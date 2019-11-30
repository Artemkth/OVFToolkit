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
            StorageArray *data;
        public:
            //constructors and other general utility
            VField();
            ~VField();
            OVFHeader header{};
            //number of bytes of current internally stored data
            std::size_t curDataInternalSize() const;
            //is data present
            bool isDataPresent() const; 
            //clearing out the storage
            void clearData();
            
            //data access methods
            //setting data to whatever, 
            void setData(float*, const std::size_t);
            void setData(double*, const std::size_t);
            //setting specific elements
            void setPoint(const std::size_t, const float);
            void setPoint(const std::size_t, const double);

            //get data
            template <typename T>
            const T* getData() const;
            //get a copy
            template <typename T>
            T* getDataCopy();
    };
    //templates for getting the internal array
    template<>
    const float* VField::getData<float>() const;
    template<>
    const double* VField::getData<double>() const;

    //template for getting a copy of internal array, changes to it will be not regarded
    template<>
    float* VField::getDataCopy<float>();
    template<>
    double* VField::getDataCopy<double>();
}

