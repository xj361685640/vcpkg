#include "pch.h"

#include <vcpkg/commands.h>
#include <vcpkg/export.h>
#include <vcpkg/export.ifw.h>
#include <vcpkg/install.h>

namespace vcpkg::Export::IFW
{
    using Dependencies::ExportPlanAction;
    using Dependencies::ExportPlanType;
    using Install::InstallDir;

    static std::string create_release_date()
    {
        const tm date_time = System::get_current_date_time();

        // Format is: YYYY-mm-dd
        // 10 characters + 1 null terminating character will be written for a total of 11 chars
        char mbstr[11];
        const size_t bytes_written = std::strftime(mbstr, sizeof(mbstr), "%Y-%m-%d", &date_time);
        Checks::check_exit(VCPKG_LINE_INFO,
                           bytes_written == 10,
                           "Expected 10 bytes to be written, but %u were written",
                           bytes_written);
        const std::string date_time_as_string(mbstr);
        return date_time_as_string;
    }

    std::string safe_rich_from_plain_text(const std::string& text)
    {
        // match standalone ampersand, no HTML number or name
        std::regex standalone_ampersand(R"###(&(?!(#[0-9]+|\w+);))###");

        return std::regex_replace(text, standalone_ampersand, "&amp;");
    }

    fs::path get_packages_dir_path(const std::string& export_id, const Options& ifw_options, const VcpkgPaths& paths)
    {
        return ifw_options.maybe_packages_dir_path.has_value()
                   ? fs::path(ifw_options.maybe_packages_dir_path.value_or_exit(VCPKG_LINE_INFO))
                   : paths.root / (export_id + "-ifw-packages");
    }

    fs::path get_repository_dir_path(const std::string& export_id, const Options& ifw_options, const VcpkgPaths& paths)
    {
        return ifw_options.maybe_repository_dir_path.has_value()
                   ? fs::path(ifw_options.maybe_repository_dir_path.value_or_exit(VCPKG_LINE_INFO))
                   : paths.root / (export_id + "-ifw-repository");
    }

    fs::path get_config_file_path(const std::string& export_id, const Options& ifw_options, const VcpkgPaths& paths)
    {
        return ifw_options.maybe_config_file_path.has_value()
                   ? fs::path(ifw_options.maybe_config_file_path.value_or_exit(VCPKG_LINE_INFO))
                   : paths.root / (export_id + "-ifw-configuration.xml");
    }

    fs::path get_installer_file_path(const std::string& export_id, const Options& ifw_options, const VcpkgPaths& paths)
    {
        return ifw_options.maybe_installer_file_path.has_value()
                   ? fs::path(ifw_options.maybe_installer_file_path.value_or_exit(VCPKG_LINE_INFO))
                   : paths.root / (export_id + "-ifw-installer.exe");
    }

    fs::path export_real_package(const fs::path& ifw_packages_dir_path,
                                 const ExportPlanAction& action,
                                 Files::Filesystem& fs)
    {
        std::error_code ec;

        const BinaryParagraph& binary_paragraph =
            action.any_paragraph.binary_control_file.value_or_exit(VCPKG_LINE_INFO).core_paragraph;

        // Prepare meta dir
        const fs::path package_xml_file_path =
            ifw_packages_dir_path /
            Strings::format("packages.%s.%s", action.spec.name(), action.spec.triplet().canonical_name()) / "meta" /
            "package.xml";
        const fs::path package_xml_dir_path = package_xml_file_path.parent_path();
        fs.create_directories(package_xml_dir_path, ec);
        Checks::check_exit(VCPKG_LINE_INFO,
                           !ec,
                           "Could not create directory for package file %s",
                           package_xml_file_path.generic_string());

        auto deps = Strings::join(
            ",", binary_paragraph.depends, [](const std::string& dep) { return "packages." + dep + ":"; });

        if (!deps.empty()) deps = "\n    <Dependencies>" + deps + "</Dependencies>";

        fs.write_contents(package_xml_file_path,
                          Strings::format(
                              R"###(<?xml version="1.0"?>
<Package>
    <DisplayName>%s</DisplayName>
    <Version>%s</Version>
    <ReleaseDate>%s</ReleaseDate>
    <AutoDependOn>packages.%s:,triplets.%s:</AutoDependOn>%s
    <Virtual>true</Virtual>
</Package>
)###",
                              action.spec.to_string(),
                              binary_paragraph.version,
                              create_release_date(),
                              action.spec.name(),
                              action.spec.triplet().canonical_name(),
                              deps));

        // Return dir path for export package data
        return ifw_packages_dir_path /
               Strings::format("packages.%s.%s", action.spec.name(), action.spec.triplet().canonical_name()) / "data" /
               "installed";
    }

    void export_unique_packages(const fs::path& raw_exported_dir_path,
                                std::map<std::string, const ExportPlanAction*> unique_packages,
                                Files::Filesystem& fs)
    {
        std::error_code ec;

        // packages

        fs::path package_xml_file_path = raw_exported_dir_path / "packages" / "meta" / "package.xml";
        fs::path package_xml_dir_path = package_xml_file_path.parent_path();
        fs.create_directories(package_xml_dir_path, ec);
        Checks::check_exit(VCPKG_LINE_INFO,
                           !ec,
                           "Could not create directory for package file %s",
                           package_xml_file_path.generic_string());
        fs.write_contents(package_xml_file_path,
                          Strings::format(
                              R"###(<?xml version="1.0"?>
<Package>
    <DisplayName>Packages</DisplayName>
    <Version>1.0.0</Version>
    <ReleaseDate>%s</ReleaseDate>
</Package>
)###",
                              create_release_date()));

        for (auto package = unique_packages.begin(); package != unique_packages.end(); ++package)
        {
            const ExportPlanAction& action = *(package->second);
            const BinaryParagraph& binary_paragraph =
                action.any_paragraph.binary_control_file.value_or_exit(VCPKG_LINE_INFO).core_paragraph;

            package_xml_file_path =
                raw_exported_dir_path / Strings::format("packages.%s", package->first) / "meta" / "package.xml";
            package_xml_dir_path = package_xml_file_path.parent_path();
            fs.create_directories(package_xml_dir_path, ec);
            Checks::check_exit(VCPKG_LINE_INFO,
                               !ec,
                               "Could not create directory for package file %s",
                               package_xml_file_path.generic_string());

            fs.write_contents(package_xml_file_path,
                              Strings::format(
                                  R"###(<?xml version="1.0"?>
<Package>
    <DisplayName>%s</DisplayName>
    <Description>%s</Description>
    <Version>%s</Version>
    <ReleaseDate>%s</ReleaseDate>
</Package>
)###",
                                  action.spec.name(),
                                  safe_rich_from_plain_text(binary_paragraph.description),
                                  binary_paragraph.version,
                                  create_release_date()));
        }
    }

    void export_unique_triplets(const fs::path& raw_exported_dir_path,
                                std::set<std::string> unique_triplets,
                                Files::Filesystem& fs)
    {
        std::error_code ec;

        // triplets

        fs::path package_xml_file_path = raw_exported_dir_path / "triplets" / "meta" / "package.xml";
        fs::path package_xml_dir_path = package_xml_file_path.parent_path();
        fs.create_directories(package_xml_dir_path, ec);
        Checks::check_exit(VCPKG_LINE_INFO,
                           !ec,
                           "Could not create directory for package file %s",
                           package_xml_file_path.generic_string());
        fs.write_contents(package_xml_file_path,
                          Strings::format(
                              R"###(<?xml version="1.0"?>
<Package>
    <DisplayName>Triplets</DisplayName>
    <Version>1.0.0</Version>
    <ReleaseDate>%s</ReleaseDate>
</Package>
)###",
                              create_release_date()));

        for (const std::string& triplet : unique_triplets)
        {
            package_xml_file_path =
                raw_exported_dir_path / Strings::format("triplets.%s", triplet) / "meta" / "package.xml";
            package_xml_dir_path = package_xml_file_path.parent_path();
            fs.create_directories(package_xml_dir_path, ec);
            Checks::check_exit(VCPKG_LINE_INFO,
                               !ec,
                               "Could not create directory for package file %s",
                               package_xml_file_path.generic_string());
            fs.write_contents(package_xml_file_path,
                              Strings::format(
                                  R"###(<?xml version="1.0"?>
<Package>
    <DisplayName>%s</DisplayName>
    <Version>1.0.0</Version>
    <ReleaseDate>%s</ReleaseDate>
</Package>
)###",
                                  triplet,
                                  create_release_date()));
        }
    }

    void export_integration(const fs::path& raw_exported_dir_path, Files::Filesystem& fs)
    {
        std::error_code ec;

        // integration
        fs::path package_xml_file_path = raw_exported_dir_path / "integration" / "meta" / "package.xml";
        fs::path package_xml_dir_path = package_xml_file_path.parent_path();
        fs.create_directories(package_xml_dir_path, ec);
        Checks::check_exit(VCPKG_LINE_INFO,
                           !ec,
                           "Could not create directory for package file %s",
                           package_xml_file_path.generic_string());

        fs.write_contents(package_xml_file_path,
                          Strings::format(
                              R"###(<?xml version="1.0"?>
<Package>
    <DisplayName>Integration</DisplayName>
    <Version>1.0.0</Version>
    <ReleaseDate>%s</ReleaseDate>
</Package>
)###",
                              create_release_date()));
    }

    void export_config(const std::string& export_id, const Options& ifw_options, const VcpkgPaths& paths)
    {
        std::error_code ec;
        Files::Filesystem& fs = paths.get_filesystem();

        const fs::path config_xml_file_path = get_config_file_path(export_id, ifw_options, paths);

        fs::path config_xml_dir_path = config_xml_file_path.parent_path();
        fs.create_directories(config_xml_dir_path, ec);
        Checks::check_exit(VCPKG_LINE_INFO,
                           !ec,
                           "Could not create directory for configuration file %s",
                           config_xml_file_path.generic_string());

        std::string formatted_repo_url;
        std::string ifw_repo_url = ifw_options.maybe_repository_url.value_or("");
        if (!ifw_repo_url.empty())
        {
            formatted_repo_url = Strings::format(R"###(
    <RemoteRepositories>
        <Repository>
            <Url>%s</Url>
        </Repository>
    </RemoteRepositories>)###",
                                                 ifw_repo_url);
        }

        fs.write_contents(config_xml_file_path,
                          Strings::format(
                              R"###(<?xml version="1.0"?>
<Installer>
    <Name>vcpkg</Name>
    <Version>1.0.0</Version>
    <StartMenuDir>vcpkg</StartMenuDir>
    <TargetDir>@RootDir@/src/vcpkg</TargetDir>%s
</Installer>
)###",
                              formatted_repo_url));
    }

    void export_maintenance_tool(const fs::path& ifw_packages_dir_path, const VcpkgPaths& paths)
    {
        System::println("Exporting maintenance tool... ");

        std::error_code ec;
        Files::Filesystem& fs = paths.get_filesystem();

        const fs::path& installerbase_exe = paths.get_ifw_installerbase_exe();
        fs::path tempmaintenancetool = ifw_packages_dir_path / "maintenance" / "data" / "tempmaintenancetool.exe";
        fs.create_directories(tempmaintenancetool.parent_path(), ec);
        Checks::check_exit(VCPKG_LINE_INFO,
                           !ec,
                           "Could not create directory for package file %s",
                           tempmaintenancetool.generic_string());
        fs.copy_file(installerbase_exe, tempmaintenancetool, fs::copy_options::overwrite_existing, ec);
        Checks::check_exit(
            VCPKG_LINE_INFO, !ec, "Could not write package file %s", tempmaintenancetool.generic_string());

        fs::path package_xml_file_path = ifw_packages_dir_path / "maintenance" / "meta" / "package.xml";
        fs::path package_xml_dir_path = package_xml_file_path.parent_path();
        fs.create_directories(package_xml_dir_path, ec);
        Checks::check_exit(VCPKG_LINE_INFO,
                           !ec,
                           "Could not create directory for package file %s",
                           package_xml_file_path.generic_string());
        fs.write_contents(package_xml_file_path,
                          Strings::format(
                              R"###(<?xml version="1.0"?>
<Package>
    <DisplayName>Maintenance Tool</DisplayName>
    <Description>Maintenance Tool</Description>
    <Version>1.0.0</Version>
    <ReleaseDate>%s</ReleaseDate>
    <Script>maintenance.qs</Script>
    <Essential>true</Essential>
    <Virtual>true</Virtual>
    <ForcedInstallation>true</ForcedInstallation>
</Package>
)###",
                              create_release_date()));
        const fs::path script_source = paths.root / "scripts" / "ifw" / "maintenance.qs";
        const fs::path script_destination = ifw_packages_dir_path / "maintenance" / "meta" / "maintenance.qs";
        fs.copy_file(script_source, script_destination, fs::copy_options::overwrite_existing, ec);
        Checks::check_exit(
            VCPKG_LINE_INFO, !ec, "Could not write package file %s", script_destination.generic_string());

        System::println("Exporting maintenance tool... done");
    }

    void do_repository(const std::string& export_id, const Options& ifw_options, const VcpkgPaths& paths)
    {
        const fs::path& repogen_exe = paths.get_ifw_repogen_exe();
        const fs::path packages_dir = get_packages_dir_path(export_id, ifw_options, paths);
        const fs::path repository_dir = get_repository_dir_path(export_id, ifw_options, paths);

        System::println("Generating repository %s...", repository_dir.generic_string());

        std::error_code ec;
        Files::Filesystem& fs = paths.get_filesystem();

        fs.remove_all(repository_dir, ec);
        Checks::check_exit(
            VCPKG_LINE_INFO, !ec, "Could not remove outdated repository directory %s", repository_dir.generic_string());

        const auto cmd_line = Strings::format(R"("%s" --packages "%s" "%s" > nul)",
                                              repogen_exe.u8string(),
                                              packages_dir.u8string(),
                                              repository_dir.u8string());

        const int exit_code = System::cmd_execute_clean(cmd_line);
        Checks::check_exit(VCPKG_LINE_INFO, exit_code == 0, "Error: IFW repository generating failed");

        System::println(System::Color::success, "Generating repository %s... done.", repository_dir.generic_string());
    }

    void do_installer(const std::string& export_id, const Options& ifw_options, const VcpkgPaths& paths)
    {
        const fs::path& binarycreator_exe = paths.get_ifw_binarycreator_exe();
        const fs::path config_file = get_config_file_path(export_id, ifw_options, paths);
        const fs::path packages_dir = get_packages_dir_path(export_id, ifw_options, paths);
        const fs::path repository_dir = get_repository_dir_path(export_id, ifw_options, paths);
        const fs::path installer_file = get_installer_file_path(export_id, ifw_options, paths);

        System::println("Generating installer %s...", installer_file.generic_string());

        std::string cmd_line;

        std::string ifw_repo_url = ifw_options.maybe_repository_url.value_or("");
        if (!ifw_repo_url.empty())
        {
            cmd_line = Strings::format(R"("%s" --online-only --config "%s" --repository "%s" "%s" > nul)",
                                       binarycreator_exe.u8string(),
                                       config_file.u8string(),
                                       repository_dir.u8string(),
                                       installer_file.u8string());
        }
        else
        {
            cmd_line = Strings::format(R"("%s" --config "%s" --packages "%s" "%s" > nul)",
                                       binarycreator_exe.u8string(),
                                       config_file.u8string(),
                                       packages_dir.u8string(),
                                       installer_file.u8string());
        }

        const int exit_code = System::cmd_execute_clean(cmd_line);
        Checks::check_exit(VCPKG_LINE_INFO, exit_code == 0, "Error: IFW installer generating failed");

        System::println(System::Color::success, "Generating installer %s... done.", installer_file.generic_string());
    }

    void do_export(const std::vector<ExportPlanAction>& export_plan,
                   const std::string& export_id,
                   const Options& ifw_options,
                   const VcpkgPaths& paths)
    {
        std::error_code ec;
        Files::Filesystem& fs = paths.get_filesystem();

        // Prepare packages directory
        const fs::path ifw_packages_dir_path = get_packages_dir_path(export_id, ifw_options, paths);

        fs.remove_all(ifw_packages_dir_path, ec);
        Checks::check_exit(VCPKG_LINE_INFO,
                           !ec,
                           "Could not remove outdated packages directory %s",
                           ifw_packages_dir_path.generic_string());

        fs.create_directory(ifw_packages_dir_path, ec);
        Checks::check_exit(
            VCPKG_LINE_INFO, !ec, "Could not create packages directory %s", ifw_packages_dir_path.generic_string());

        // Export maintenance tool
        export_maintenance_tool(ifw_packages_dir_path, paths);

        System::println("Exporting packages %s... ", ifw_packages_dir_path.generic_string());

        // execute the plan
        std::map<std::string, const ExportPlanAction*> unique_packages;
        std::set<std::string> unique_triplets;
        for (const ExportPlanAction& action : export_plan)
        {
            if (action.plan_type != ExportPlanType::ALREADY_BUILT)
            {
                Checks::unreachable(VCPKG_LINE_INFO);
            }

            const std::string display_name = action.spec.to_string();
            System::println("Exporting package %s... ", display_name);

            const BinaryParagraph& binary_paragraph =
                action.any_paragraph.binary_control_file.value_or_exit(VCPKG_LINE_INFO).core_paragraph;

            unique_packages[action.spec.name()] = &action;
            unique_triplets.insert(action.spec.triplet().canonical_name());

            // Export real package and return data dir for installation
            fs::path ifw_package_dir_path = export_real_package(ifw_packages_dir_path, action, fs);

            // Copy package data
            const InstallDir dirs = InstallDir::from_destination_root(ifw_package_dir_path,
                                                                      action.spec.triplet().to_string(),
                                                                      ifw_package_dir_path / "vcpkg" / "info" /
                                                                          (binary_paragraph.fullstem() + ".list"));

            Install::install_files_and_write_listfile(paths.get_filesystem(), paths.package_dir(action.spec), dirs);
            System::println("Exporting package %s... done", display_name);
        }

        System::println("Exporting packages %s... done", ifw_packages_dir_path.generic_string());

        const fs::path config_file = get_config_file_path(export_id, ifw_options, paths);

        System::println("Generating configuration %s...", config_file.generic_string());

        // Unique packages
        export_unique_packages(ifw_packages_dir_path, unique_packages, fs);

        // Unique triplets
        export_unique_triplets(ifw_packages_dir_path, unique_triplets, fs);

        // Copy files needed for integration
        export_integration_files(ifw_packages_dir_path / "integration" / "data", paths);
        // Integration
        export_integration(ifw_packages_dir_path, fs);

        // Configuration
        export_config(export_id, ifw_options, paths);

        System::println("Generating configuration %s... done.", config_file.generic_string());

        // Do repository (optional)
        std::string ifw_repo_url = ifw_options.maybe_repository_url.value_or("");
        if (!ifw_repo_url.empty())
        {
            do_repository(export_id, ifw_options, paths);
        }

        // Do installer
        do_installer(export_id, ifw_options, paths);
    }
}
