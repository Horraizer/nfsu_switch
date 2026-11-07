#pragma once
#include "nfsu/career.h"
namespace nfsu {
// Display names never become paths. ID0 references the pre-profile career.
struct CareerProfileEntry {
    unsigned id=0;
    std::string name;
    std::filesystem::path base;
    std::string saved_at;
};
class CareerProfiles {
public:
    explicit CareerProfiles(std::filesystem::path root):root_(std::move(root)){}
    bool list(std::vector<CareerProfileEntry>&,unsigned& selected,std::string& error)const;
    bool create(const std::string& name,const CareerCatalog&,CareerProfileEntry&,std::string& error);
    bool select(unsigned id,std::string& error);
    static bool valid_name(const std::string&)noexcept;
private:
    std::filesystem::path root_;
};
}
