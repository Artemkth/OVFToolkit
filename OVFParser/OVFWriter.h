#pragma once
//Declaration of file-exporting interfaces for the vector-field files
#include<ostream>
#include"VField.h"
#include"OVFVersion.h"

//for outputting contents of VFields
std::string WriteSegment(std::ostream&, const VField&) noexcept;

//writing a file with a single field
std::string WriteOVF(const std::string&, const VField&) noexcept;

//writing a whole bunch of files
template<typename T>
inline std::string WriteOVF(T begin, T end) noexcept;

