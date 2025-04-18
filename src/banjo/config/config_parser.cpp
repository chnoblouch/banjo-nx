#include "config_parser.hpp"
#include "banjo/target/target_description.hpp"

namespace banjo {

namespace lang {

static const std::string ARG_ARCH = "arch";
static const std::string ARG_OS = "os";
static const std::string ARG_ENV = "env";
static const std::string ARG_TYPE = "type";
static const std::string ARG_OPTIONAL_SEMICOLONS = "optional-semicolons";
static const std::string ARG_OPT_LEVEL = "opt-level";
static const std::string ARG_NO_COLOR = "no-color";
static const std::string ARG_PIC = "pic";
static const std::string ARG_CODE_MODEL = "code-model";
static const std::string ARG_HOT_RELOAD = "hot-reload";
static const std::string ARG_TESTING = "testing";
static const std::string ARG_FORCE_ASM = "force-asm";
static const std::string ARG_DISABLE_STD = "disable-std";
static const std::string ARG_DEBUG = "debug";
static const std::string ARG_PATH = "path";

ConfigParser::ConfigParser() {
    arg_parser.add_value(ARG_ARCH, "x86_64")
        .add_value(ARG_OS, "windows")
        .add_value(ARG_ENV, "msvc")
        .add_value(ARG_TYPE, "executable")
        .add_flag(ARG_OPTIONAL_SEMICOLONS)
        .add_value(ARG_OPT_LEVEL, "0")
        .add_flag(ARG_NO_COLOR)
        .add_flag(ARG_PIC)
        .add_value(ARG_CODE_MODEL, "")
        .add_flag(ARG_HOT_RELOAD)
        .add_flag(ARG_TESTING)
        .add_flag(ARG_FORCE_ASM)
        .add_flag(ARG_DISABLE_STD)
        .add_flag(ARG_DEBUG)
        .add_list(ARG_PATH);
}

Config ConfigParser::parse(int argc, char **argv) {
    ParsedArgs args = arg_parser.parse(argc, argv);
    return create_config(args);
}

Config ConfigParser::create_config(const ParsedArgs &args) {
    Config config;
    config.target = create_target(args);
    config.optional_semicolons = args.flags.at(ARG_OPTIONAL_SEMICOLONS);
    config.opt_level = std::stoi(args.values.at(ARG_OPT_LEVEL));
    config.color_diagnostics = !args.flags.at(ARG_NO_COLOR);
    config.pic = args.flags.at(ARG_PIC);
    config.hot_reload = args.flags.at(ARG_HOT_RELOAD);
    config.testing = args.flags.at(ARG_TESTING);
    config.force_asm = args.flags.at(ARG_FORCE_ASM);
    config.disable_std = args.flags.at(ARG_DISABLE_STD);
    config.debug = args.flags.at(ARG_DEBUG);

    const std::string &code_model = args.values.at(ARG_CODE_MODEL);
    if (code_model == "small") config.code_model = {target::CodeModel::SMALL};
    else if (code_model == "large") config.code_model = {target::CodeModel::LARGE};
    else config.code_model = {};

    for (const std::string &path : args.lists.at(ARG_PATH)) {
        config.paths.push_back(path);
    }

    return config;
}

target::TargetDescription ConfigParser::create_target(const ParsedArgs &args) {
    target::Architecture arch;
    target::OperatingSystem os;
    target::Environment env;

    const std::string &arch_name = args.values.at(ARG_ARCH);
    const std::string &os_name = args.values.at(ARG_OS);
    const std::string &env_name = args.values.at(ARG_ENV);

    if (arch_name == "x86_64") arch = target::Architecture::X86_64;
    else if (arch_name == "aarch64") arch = target::Architecture::AARCH64;
    else arch = target::Architecture::X86_64;

    if (os_name == "windows") os = target::OperatingSystem::WINDOWS;
    else if (os_name == "linux") os = target::OperatingSystem::LINUX;
    else if (os_name == "macos") os = target::OperatingSystem::MACOS;
    else if (os_name == "android") os = target::OperatingSystem::ANDROID;
    else if (os_name == "ios") os = target::OperatingSystem::IOS;
    else if (os_name == "nintendoswitch") os = target::OperatingSystem::NINTENDOSWITCH;
    else os = target::OperatingSystem::WINDOWS;

    if (env_name == "msvc") env = target::Environment::MSVC;
    else if (env_name == "gnu") env = target::Environment::GNU;
    else if (env_name == "devkitpro") env = target::Environment::DEVKITPRO;
    else env = target::Environment::NONE;

    return target::TargetDescription(arch, os, env);
}

} // namespace lang

} // namespace banjo
