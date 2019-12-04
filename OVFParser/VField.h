//header file for the main vector field storage container, template class VField
#pragma once
#include"OVFHeader.h"

namespace VField{
    class VField
    {
        private:
            //details of data storage thingie defined outside
            //data is stored internally as a single array of homogenious-type values
            struct StorageArray;
            StorageArray *data{nullptr};
        public:
            //constructors and other general utility
            VField();
            explicit VField(const associatedType_t<pType::String>& version): VField()
            {Header.set(OVFParameter::VersionString, version);}
            ~VField();
            //copy and move c-tors
            VField(const VField&);
            VField& operator=(const VField&);
            //I would like to move it move it lol
            VField(VField&& ref): data(ref.data), Header(std::move(ref.Header))
            {ref.data = nullptr;}
            VField& operator=(VField&&);
            
            OVFHeader Header{};
            //number of bytes of current internally stored data
            std::size_t curDataInternalSize() const;
            //and number of data points
            std::size_t curDataPoints() const;
            //is data present
            bool isDataPresent() const; 
            //clearing out the storage
            void clearData();
            
            //data access methods
            //setting data to whatever, 
            void setData(float*, const std::size_t&);
            void setData(double*, const std::size_t&);
            //same but with a copy, indicated by pointer being constant
            void setData(const float*, const std::size_t&);
            void setData(const double*, const std::size_t&);
            //setting specific elements
            void setPoint(const std::size_t&, const float&);
            void setPoint(const std::size_t&, const double&);
            //get data
            template <typename T>
            const T* getData() const;
            //get a copy
            template <typename T>
            T* getDataCopy() const;
            //getting a data point method is redundant since you can do it on your own
            
            //interfaces for validation and deduction
            //defined and realized in OVFGrammar.cpp!
            bool isAddressable() const;                                     //validate if there is enough information to traverse internal array
            bool isValid();                                                 //check if vector field is in spec
            std::string ValidationReport();                                 //full report of validation results, run validation if needed
            bool DeduceField(const OVFParameter&, bool UseDefault = true);  //try to deduce a field from data already known, use defaults for insignificant data if needed
            std::string DeduceRecursively(const std::size_t& max_iter = 5); //try to deduce out all of the missing required fields
    };
    
    //available specializations
    //templates for getting the internal array
    template<>
    const float* VField::getData<float>() const;
    template<>
    const double* VField::getData<double>() const;

    //template for getting a copy of internal array, changes to it will be not regarded
    template<>
    float* VField::getDataCopy<float>() const;
    template<>
    double* VField::getDataCopy<double>() const;
}
