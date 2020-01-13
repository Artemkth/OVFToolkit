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

    return 0;
}
