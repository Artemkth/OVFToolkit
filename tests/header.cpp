#include<iostream>
#include<variant>
#include<optional>
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
    testHeader.clear(VField::OVFParameter::Desc);
    testHeader.clear(VField::OVFParameter::Xnodes);
    testHeader.clear(VField::OVFParameter::Xstep);
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
    testHeader.clear(VField::OVFParameter::Mtype);
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
    if (emptyHeader != testHeader || copy2 != VField::OVFHeader{})
    {
        std::cerr<< "Swap mangled the data!\n";
        return 12;
    }
    //check reset
    emptyHeader.reset();
    if(emptyHeader != VField::OVFHeader{})
    {
        std::cerr<< "Reset didn't clear the data completely!\n";
        return 13;
    }

    //test exception throws
    try{
        //Reading wrong type of variable through getXXXX interface
        testHeader.getUint(VField::OVFParameter::VersionString);
        std::cerr << "Exception was expected for reading wrong type!\n";
        return 14;
    } catch (const std::bad_variant_access&) {}
    try{
        //Writing a wrong type of variable through set interface
        testHeader.set(VField::OVFParameter::Desc, 1.0f);
        std::cerr << "Exception was expected for setting a wrong type!\n";
        return 15;
    } catch (const std::bad_variant_access&) {}
    try{
        //Reading wrong type of variable through the 'at<>' interface
        testHeader.at<VField::pType::Uint>(VField::OVFParameter::VersionString);
        std::cerr << "Exception was expected for reading wrong type!\n";
        return 16;
    } catch (const std::bad_variant_access&) {}
    try{
        //Reading unitialized variable
        testHeader.getUint(VField::OVFParameter::Ynodes);
        std::cerr << "Exception was expected for accessing unitialized variable!\n";
        return 17;
    } catch (const std::bad_optional_access&) {}
    try{
        //Reading unitialized variable from constant header
        const_cast<const VField::OVFHeader&>(testHeader).at<VField::pType::Uint>(VField::OVFParameter::Ynodes);
        std::cerr << "Exception was expected for accessing unitialized variable!\n";
        return 18;
    } catch (const std::logic_error&) {}

    return 0;
}
