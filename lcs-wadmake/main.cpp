#include <cstdio>
#include <lcs/error.hpp>
#include <lcs/progress.hpp>
#include <lcs/wad.hpp>
#include <lcs/wadmake.hpp>

using namespace LCS;

#ifdef WIN32
#define print_path(name, path) wprintf(L"%s: %s\n", L ## name, path.c_str())
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <processenv.h>
#define make_main(body) int main() { auto argc = 0; auto argv = CommandLineToArgvW(GetCommandLineW(), &argc); body }
#else
#define print_path(name, path) printf("%s%s\n", name, path.c_str())
#define make_main(body) int main(int argc, char** argv) { body }
#endif

make_main({
    try {
        if (argc < 2) {
            throw std::runtime_error("lolcustomskin-wadmake.exe <folder path> <optional: wad path>");
        }
        fs::path source = argv[1];
        fs::path dest;
        if (argc > 2) {
            dest = argv[2];
        } else {
            dest = source;
            dest.replace_extension(".wad.client");
        }
        fs::create_directories(dest.parent_path());

        print_path("Reading", source);
        WadMake wadmake(source, nullptr, false);
        print_path("Packing", dest);
        Progress progress = {};
        wadmake.write(dest, progress);
        printf("Finished!\n");
    } catch(std::runtime_error const& error) {
        error_print(error);
        if (argc < 3) {
            printf("Press enter to exit...!\n");
            getc(stdin);
        }
    }
})
