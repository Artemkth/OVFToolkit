#include<iostream>
#include<OVFHeader.h>

int main()
{
    VField::OVFHeader testHeader("# OOMMF OVF 2.0");
    //and now fill in some fields, bare minimum to see if it was successfull
    const std::string testString{  "Just of-a-wall header" };
    std::size_t testUnsigned { 11 };
    double testDouble { 123456789.0 };

    testHeader.set(VField::OVFParameter::Desc, testString);
    testHeader.set(VField::OVFParameter::Xnodes, testUnsigned);
    testHeader.set(VField::OVFParameter::Xstep, testDouble);

    //and then immediately test what we have set
    if(testHeader.getString(VField::OVFParameter::Desc) != testString)
    {
        std::cerr << "String value got corrupted!\n";
        return 1;
    }
    if(testHeader.getUint(VField::OVFParameter::Xnodes) != testUnsigned)
    {
        std::cerr << "Integer value got corrupted!\n";
        return 1;
    }
    if(testHeader.getFloat(VField::OVFParameter::Xstep) != testDouble)
    {
        std::cerr << "Floating-point value got corrupted!\n";
        return 1;
    }

    return 0;
}
