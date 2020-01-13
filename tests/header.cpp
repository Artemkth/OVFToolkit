#include<iostream>
#include<OVFHeader.h>

int main()
{
    VField::OVFHeader testHeader("# OOMMF OVF 2.0");
    //and now fill in some fields, bare minimum to see if it was successfull
    const std::string testString{  "Just of-a-wall header" };
    std::size_t testUnsigned { 11 };
    double testDouble { 123456789.0 };

    //test if fields we are about to set weren't set previously
    if( testHeader.isSet(VField::OVFParameter::Desc) ||
        testHeader.isSet(VField::OVFParameter::Xnodes) ||
        testHeader.isSet(VField::OVFParameter::Xstep) )
    {
        std::cerr << "Some fields were initialized incorrectly as set!\n";
        return 1;
    }
    testHeader.set(VField::OVFParameter::Desc, testString);
    testHeader.set(VField::OVFParameter::Xnodes, testUnsigned);
    testHeader.set(VField::OVFParameter::Xstep, testDouble);
    //test if the fields were set after the manipulation
    if( !( testHeader.isSet(VField::OVFParameter::Desc) ||
           testHeader.isSet(VField::OVFParameter::Xnodes) ||
           testHeader.isSet(VField::OVFParameter::Xstep) ) )
    {
        std::cerr << "Some fields failed to initialize!\n";
        return 2;
    }

    //and then immediately test what we have set
    if(testHeader.getString(VField::OVFParameter::Desc) != testString)
    {
        std::cerr << "String value got corrupted!\n";
        return 3;
    }
    if(testHeader.getUint(VField::OVFParameter::Xnodes) != testUnsigned)
    {
        std::cerr << "Integer value got corrupted!\n";
        return 3;
    }
    if(testHeader.getFloat(VField::OVFParameter::Xstep) != testDouble)
    {
        std::cerr << "Floating-point value got corrupted!\n";
        return 3;
    }

    return 0;
}
