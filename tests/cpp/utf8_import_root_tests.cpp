// Real C ABI, unique pure-source modules and a copied actual native package.
// The unique package name cannot be satisfied by an implicit SDK module root.
#include "xlang3/xlang3.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
namespace fs=std::filesystem;
struct Fixture {
    fs::path parent=fs::canonical(fs::temp_directory_path()),root,previous=fs::current_path();
    Fixture(){std::random_device random;for(int i=0;i<32;++i){auto p=parent/fs::u8path(std::string("xlang3-capi-utf8-")+std::to_string(random())+u8"-\u5199-\U0001f600");if(fs::create_directory(p)){root=fs::canonical(p);return;}}throw std::runtime_error("Cannot create owned fixture");}
    ~Fixture(){try{fs::current_path(previous);if(!root.empty()&&root.is_absolute()&&root.parent_path()==parent&&root.filename().u8string().find("xlang3-capi-utf8-")==0&&fs::canonical(root)==root&&!fs::is_symlink(fs::symlink_status(root))){std::error_code error;fs::remove_all(root,error);}}catch(...){}}
};
int main(int argc,char** argv){if(argc!=2)return 2;try{Fixture fixture;const auto package=fixture.root/"xlang_utf8_module_probe.x3pkg.dll";fs::copy_file(fs::u8path(argv[1]),package);std::ofstream(fixture.root/"xlang_utf8_source_probe.py")<<"marker = 741\n";std::ofstream(fixture.root/"xlang_utf8_file_probe.py")<<"marker = 751\n";std::ofstream(fixture.root/"entry.py")<<"import xlang_utf8_file_probe\nxlang_utf8_file_probe.marker\n";
    fs::current_path(fixture.root);
    {X::Runtime runtime;X::Module sys(runtime,"sys");if(sys["prefix"].ToString().empty())throw std::runtime_error("Unicode initialization lost sys.prefix");runtime.AddImportRoot(fixture.root.u8string());X::Module source(runtime,"xlang_utf8_source_probe");if(source["marker"].ToInt64()!=741)throw std::runtime_error("UTF-8 source root failed");X::Module native(runtime,"xlang_utf8_module_probe");if(native["__xlang3_package__"].ToString()!="xlang_json"||fs::u8path(native["__xlang3_file__"].ToString()).lexically_normal()!=package.lexically_normal())throw std::runtime_error("Native UTF-8 package path or metadata failed");}
    {X::Runtime runtime;runtime.EvalFile((fixture.root/"entry.py").u8string());X::Module source(runtime,"xlang_utf8_file_probe");if(source["marker"].ToInt64()!=751)throw std::runtime_error("UTF-8 source-file parent import failed");}
    std::cout<<"Actual C API Unicode initial working directory/sys.prefix, UTF-8 native/source roots, native package metadata and source-file parent imports passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
