#include<iostream>
#include<OVFHeader.h>

int main()
{
    VField::OVFHeader testHeader("# OOMMF OVF 2.0");
    //and now fill in some fields, bare minimum to see if it was successfull
    const std::string testString{  "Just of-a-wall header" };
    const std::size_t testUnsigned { 11 };
    const double testDouble { 123456789.0 };

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
           testHeader.isSet(VField::OVFParameter::Xstep) ) &&
          !testHeader.isSet(VField::OVFParameter::Mtype) )
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
    //test reference access
    const std::string testStringChange{  "OBAMA" };
    const std::size_t testUnsignedChange { 69 };
    const double testDoubleChange { 1337.8347 };
    testHeader.at<VField::pType::String>(VField::OVFParameter::Desc) = testStringChange;
    testHeader.at<VField::pType::Uint>(VField::OVFParameter::Xnodes) = testUnsignedChange;
    testHeader.at<VField::pType::Float>(VField::OVFParameter::Xstep) = testDoubleChange;
    if(testHeader.getString(VField::OVFParameter::Desc) != testStringChange)
    {
        std::cerr << "String value got corrupted!\n";
        return 4;
    }
    if(testHeader.getUint(VField::OVFParameter::Xnodes) != testUnsignedChange)
    {
        std::cerr << "Integer value got corrupted!\n";
        return 4;
    }
    if(testHeader.getFloat(VField::OVFParameter::Xstep) != testDoubleChange)
    {
        std::cerr << "Floating-point value got corrupted!\n";
        return 4;
    }

    //test resetting fields
    testHeader.unset(VField::OVFParameter::Desc);
    testHeader.unset(VField::OVFParameter::Xnodes);
    testHeader.unset(VField::OVFParameter::Xstep);
    if( testHeader.isSet(VField::OVFParameter::Desc) ||
        testHeader.isSet(VField::OVFParameter::Xnodes) ||
        testHeader.isSet(VField::OVFParameter::Xstep) ||
       !testHeader.isSet(VField::OVFParameter::VersionString) )
    {
        std::cerr << "Error occured while reseting fields!\n";
        return 5;
    }

    //try at access initialization for non-const OVFHeader
    testHeader.at<VField::pType::String>(VField::OVFParameter::Desc) = testString;
    testHeader.at<VField::pType::Uint>(VField::OVFParameter::Xnodes) = testUnsigned;
    testHeader.at<VField::pType::Float>(VField::OVFParameter::Xstep) = testDouble;
    //test if the fields were set after the manipulation
    if( !( testHeader.isSet(VField::OVFParameter::Desc) ||
           testHeader.isSet(VField::OVFParameter::Xnodes) ||
           testHeader.isSet(VField::OVFParameter::Xstep) ) && 
          !testHeader.isSet(VField::OVFParameter::Mtype) )
    {
        std::cerr << "Some fields failed to initialize!\n";
        return 6;
    }

    //and then immediately test what we have set
    if(testHeader.getString(VField::OVFParameter::Desc) != testString)
    {
        std::cerr << "String value got corrupted!\n";
        return 7;
    }
    if(testHeader.getUint(VField::OVFParameter::Xnodes) != testUnsigned)
    {
        std::cerr << "Integer value got corrupted!\n";
        return 7;
    }
    if(testHeader.getFloat(VField::OVFParameter::Xstep) != testDouble)
    {
        std::cerr << "Floating-point value got corrupted!\n";
        return 7;
    }

    //check access to mesh type
    testHeader.setMesh(VField::OVFHeader::MeshType::rectangular);
    if(testHeader.getMeshType() != VField::OVFHeader::MeshType::rectangular)
    {
        std::cerr << "Error setting mesh type!\n";
        return 8;
    }
    testHeader.unset(VField::OVFParameter::Mtype);
    if(testHeader.isSet(VField::OVFParameter::Mtype))
    {
        std::cerr << "Error resetting mesh type!\n";
        return 9;
    }
    //reset for copy/move ctor tests
    testHeader.setMesh(VField::OVFHeader::MeshType::rectangular);

    //copy construction
    auto copy{ testHeader };
    auto copy2  = testHeader;
    if( copy != testHeader || copy2 != testHeader)
    {
        std::cerr << "Copying corrupted the data!\n";
        return 10;
    }
    //try out the inequality operator
    copy.set(VField::OVFParameter::Desc, testStringChange);
    if( copy == testHeader )
    {
        std::cerr << "Comparison operator failure\n";
        return 11;
    }

    //check the move operators
    VField::OVFHeader emptyHeader;
    std::swap(copy2, emptyHeader);

    return 0;
}
