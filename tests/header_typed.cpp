#include <iostream>
#include <string>
#include <type_traits>

#include <OVFDictionary.h>

static_assert(std::same_as<
    VField::parameter_value_t<VField::OVFParameter::Xnodes>, std::size_t>);
static_assert(std::same_as<
    VField::parameter_value_t<VField::OVFParameter::Xstep>, double>);
static_assert(std::same_as<
    VField::parameter_value_t<VField::OVFParameter::Desc>, std::string>);
static_assert(std::same_as<
    VField::parameter_value_t<VField::OVFParameter::Mtype>, VField::MeshType>);

int main()
{
    VField::OVFHeader header;
    header.set<VField::OVFParameter::Xnodes>(42);
    header.set<VField::OVFParameter::Desc>("dictionary-derived type");
    header.set<VField::OVFParameter::Mtype>(VField::MeshType::Rectangular);

    const auto initialSize = header.size();
    if(initialSize != header.keys().size() ||
       initialSize != header.values().size() ||
       initialSize != header.items().size())
    {
        std::cerr << "Header collection sizes disagree!\n";
        return 3;
    }
    bool foundNodes = false;
    for(const auto [parameter, value] : header.items())
        if(parameter == VField::OVFParameter::Xnodes)
            foundNodes = std::get<std::size_t>(value.get()) == 42;
    if(!foundNodes)
    {
        std::cerr << "Header item iteration did not expose Xnodes!\n";
        return 4;
    }

    const auto nodes = header.lookup<VField::OVFParameter::Xnodes>();
    const auto description = header.lookup<VField::OVFParameter::Desc>();
    const auto mesh = header.lookup<VField::OVFParameter::Mtype>();
    if(!nodes || nodes->get() != 42 ||
       !description || description->get() != "dictionary-derived type" ||
       !mesh || mesh->get() != VField::MeshType::Rectangular)
    {
        std::cerr << "Dictionary-backed typed header access failed!\n";
        return 1;
    }

    header.clear<VField::OVFParameter::Xnodes>();
    if(header.contains<VField::OVFParameter::Xnodes>() ||
       header.size() + 1 != initialSize)
    {
        std::cerr << "Typed header clearing failed!\n";
        return 2;
    }
}
