#include<iostream>
#include<limits>
#include<variant>
#include<optional>
#include<OVFHeader.h>

int main()
{
    VField::OVFHeader testHeader;
    VField::OVFHeader ovf1Header{VField::OVFVersion::OVF1};
    constexpr std::string_view historicalSignature =
        "# OOMMF: rectangular mesh v0.99";
    VField::OVFHeader historicalHeader{historicalSignature};
    if(testHeader.requireAs<std::string>(VField::OVFParameter::VersionString) != "# OOMMF OVF 2.0" ||
       ovf1Header.requireAs<std::string>(VField::OVFParameter::VersionString) != "# OOMMF: rectangular mesh v1.0" ||
       historicalHeader.requireAs<std::string>(VField::OVFParameter::VersionString) != historicalSignature ||
       historicalHeader.version() != VField::OVFVersion::OVF1)
    {
        std::cerr << "Default or enum-based OVF version construction failed!\n";
        return 19;
    }
    try {
        [[maybe_unused]] VField::OVFHeader invalid{VField::OVFVersion::Unknown};
        std::cerr << "Unknown OVF version construction should throw!\n";
        return 20;
    } catch(const std::invalid_argument&) {}

    const auto initialValidation = testHeader.validate();
    if(initialValidation || initialValidation.error().report.empty() ||
       initialValidation.error().parameters.empty())
    {
        std::cerr << "Incomplete header validation did not return diagnostics!\n";
        return 21;
    }
    testHeader.clear(VField::OVFParameter::VersionString);
    const auto validationWithoutVersion = testHeader.validate();
    if(validationWithoutVersion ||
       validationWithoutVersion.error().report.find("version string was not set") == std::string::npos)
    {
        std::cerr << "Header validation returned stale diagnostics!\n";
        return 22;
    }
    testHeader.setVersion(VField::OVFVersion::OVF2);
    //and now fill in some fields, bare minimum to see if it was successfull
    const std::string testString{  "Just of-a-wall header" };
    const std::size_t testUnsigned { 11 };
    const double testDouble { 123456789.0 };

    //test if fields we are about to set weren't set previously
    if( testHeader.contains(VField::OVFParameter::Desc) ||
        testHeader.contains(VField::OVFParameter::Xnodes) ||
        testHeader.contains(VField::OVFParameter::Xstep) )
    {
        std::cerr << "Some fields were initialized incorrectly as set!\n";
        return 1;
    }
    testHeader.set(VField::OVFParameter::Desc, testString);
    testHeader.set(VField::OVFParameter::Xnodes, testUnsigned);
    testHeader.set(VField::OVFParameter::Xstep, testDouble);
    //test if the fields were set after the manipulation
    if( !( testHeader.contains(VField::OVFParameter::Desc) ||
           testHeader.contains(VField::OVFParameter::Xnodes) ||
           testHeader.contains(VField::OVFParameter::Xstep) ) &&
          !testHeader.contains(VField::OVFParameter::Mtype) )
    {
        std::cerr << "Some fields failed to initialize!\n";
        return 2;
    }

    //and then immediately test what we have set
    if(testHeader.requireAs<std::string>(VField::OVFParameter::Desc) != testString)
    {
        std::cerr << "String value got corrupted!\n";
        return 3;
    }
    if(testHeader.requireAs<std::size_t>(VField::OVFParameter::Xnodes) != testUnsigned)
    {
        std::cerr << "Integer value got corrupted!\n";
        return 3;
    }
    if(testHeader.requireAs<double>(VField::OVFParameter::Xstep) != testDouble)
    {
        std::cerr << "Floating-point value got corrupted!\n";
        return 3;
    }
    //test reference access
    const std::string testStringChange{  "OBAMA" };
    const std::size_t testUnsignedChange { 69 };
    const double testDoubleChange { 1337.8347 };
    testHeader.set(VField::OVFParameter::Desc, testStringChange);
    testHeader.set(VField::OVFParameter::Xnodes, testUnsignedChange);
    testHeader.set(VField::OVFParameter::Xstep, testDoubleChange);
    if(testHeader.requireAs<std::string>(VField::OVFParameter::Desc) != testStringChange)
    {
        std::cerr << "String value got corrupted!\n";
        return 4;
    }
    if(testHeader.requireAs<std::size_t>(VField::OVFParameter::Xnodes) != testUnsignedChange)
    {
        std::cerr << "Integer value got corrupted!\n";
        return 4;
    }
    if(testHeader.requireAs<double>(VField::OVFParameter::Xstep) != testDoubleChange)
    {
        std::cerr << "Floating-point value got corrupted!\n";
        return 4;
    }

    //test resetting fields
    testHeader.clear(VField::OVFParameter::Desc);
    testHeader.clear(VField::OVFParameter::Xnodes);
    testHeader.clear(VField::OVFParameter::Xstep);
    if( testHeader.contains(VField::OVFParameter::Desc) ||
        testHeader.contains(VField::OVFParameter::Xnodes) ||
        testHeader.contains(VField::OVFParameter::Xstep) ||
       !testHeader.contains(VField::OVFParameter::VersionString) )
    {
        std::cerr << "Error occured while reseting fields!\n";
        return 5;
    }

    //restore values through the explicit setter
    testHeader.set(VField::OVFParameter::Desc, testString);
    testHeader.set(VField::OVFParameter::Xnodes, testUnsigned);
    testHeader.set(VField::OVFParameter::Xstep, testDouble);
    //test if the fields were set after the manipulation
    if( !( testHeader.contains(VField::OVFParameter::Desc) ||
           testHeader.contains(VField::OVFParameter::Xnodes) ||
           testHeader.contains(VField::OVFParameter::Xstep) ) &&
          !testHeader.contains(VField::OVFParameter::Mtype) )
    {
        std::cerr << "Some fields failed to initialize!\n";
        return 6;
    }

    //and then immediately test what we have set
    if(testHeader.requireAs<std::string>(VField::OVFParameter::Desc) != testString)
    {
        std::cerr << "String value got corrupted!\n";
        return 7;
    }
    if(testHeader.requireAs<std::size_t>(VField::OVFParameter::Xnodes) != testUnsigned)
    {
        std::cerr << "Integer value got corrupted!\n";
        return 7;
    }
    if(testHeader.requireAs<double>(VField::OVFParameter::Xstep) != testDouble)
    {
        std::cerr << "Floating-point value got corrupted!\n";
        return 7;
    }

    //check access to mesh type
    testHeader.setMeshType(VField::MeshType::Rectangular);
    if(testHeader.meshType() != VField::MeshType::Rectangular)
    {
        std::cerr << "Error setting mesh type!\n";
        return 8;
    }
    testHeader.clear(VField::OVFParameter::Mtype);
    if(testHeader.contains(VField::OVFParameter::Mtype))
    {
        std::cerr << "Error resetting mesh type!\n";
        return 9;
    }
    //reset for copy/move ctor tests
    testHeader.setMeshType(VField::MeshType::Rectangular);

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

    const auto wrongType =
        testHeader.lookupAs<std::size_t>(VField::OVFParameter::VersionString);
    if(wrongType || wrongType.error().code != VField::HeaderAccessErrorCode::WrongType)
    {
        std::cerr << "Wrong-type lookup did not return its diagnostic!\n";
        return 14;
    }
    try{
        //Writing a wrong type of variable through set interface
        testHeader.set(VField::OVFParameter::Desc, 1.0f);
        std::cerr << "Exception was expected for setting a wrong type!\n";
        return 15;
    } catch (const std::invalid_argument&) {}

    const auto missing = testHeader.lookupAs<std::size_t>(VField::OVFParameter::Ynodes);
    if(missing || missing.error().code != VField::HeaderAccessErrorCode::MissingParameter ||
       missing.error().expected != VField::ParameterType::Unsigned)
    {
        std::cerr << "Missing-value lookup did not return its diagnostic!\n";
        return 17;
    }

    testHeader.set(VField::OVFParameter::Ystep, std::size_t{7});
    if(testHeader.requireAs<double>(VField::OVFParameter::Ystep) != 7.0)
    {
        std::cerr << "Unsigned-to-floating setter conversion failed!\n";
        return 23;
    }

    const auto unsupported = testHeader.lookup(VField::OVFParameter::Open);
    if(unsupported ||
       unsupported.error().code != VField::HeaderAccessErrorCode::UnsupportedParameter)
    {
        std::cerr << "Service parameter lookup did not return its diagnostic!\n";
        return 24;
    }

    VField::OVFHeader incompleteGrid;
    if(incompleteGrid.meshType() || incompleteGrid.pointCount() ||
       incompleteGrid.pointDimension())
    {
        std::cerr << "Incomplete grid metadata unexpectedly produced dimensions!\n";
        return 25;
    }
    incompleteGrid.setMeshType(VField::MeshType::Rectangular);
    incompleteGrid.set(VField::OVFParameter::Vdim, std::size_t{3});
    incompleteGrid.set(VField::OVFParameter::Xnodes,
                       std::numeric_limits<std::size_t>::max());
    incompleteGrid.set(VField::OVFParameter::Ynodes, std::size_t{2});
    incompleteGrid.set(VField::OVFParameter::Znodes, std::size_t{2});
    if(incompleteGrid.pointCount() || incompleteGrid.pointDimension() != 3)
    {
        std::cerr << "Grid size overflow was not rejected!\n";
        return 26;
    }

    return 0;
}
