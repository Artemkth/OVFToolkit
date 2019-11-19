#include"OVFReader.h"
#include<iostream>
#include<iomanip>
#include<fstream>
#include<list>
#include<map>
#include<array>
#include<stdexcept>
#include<regex>
#include<limits>
#include<cmath>


//boost endian conversion library setup
#include<boost/endian/conversion.hpp>


///Break compilation if the float or double are not standard,
//very sorry, the file is in binary :p
static_assert(std::numeric_limits<double>::is_iec559, "The systems double is not IEC559 compatible");
static_assert(std::numeric_limits<float>::is_iec559, "The systems float is not IEC559 compatible");
//check if numerics are double by default
static_assert(sizeof(1.0) == sizeof(double), "Double literals function unexpectedly");
//and just for kicks
static_assert(sizeof(1.0f) == sizeof(float), "Fload literals function unexpectedly");



//test constants
const float FLT_TEST = 1234567.0;
const double DBL_TEST = 123456789012345.0;


//parameter types
enum class pType {
	Other,
	Uint,
	Float,
	String
};

enum class OVFParameter {
	Open, Close, //opening and closing of the block
	Comment, //comments beginning with #
	Title, //title of the file
	Segcnt,//segment count
	Desc,//description
	Munit,//mesh unit
	Vunit,//value unit
	Vmult,//value multiplier
	Vlabels,//value labels
	Vdim, //value dimension
	Xmin, Ymin, Zmin, Xmax, Ymax, Zmax,//coordinate 
	Bound,//boundaries
	Vmax, Vmin,//boundary of values
	Mtype,//mesh type
	Pcount,//number of points
	Xbase, Ybase, Zbase,//beginning point for coords
	Xstep, Ystep, Zstep,//coordinate steps
	Xnodes, Ynodes, Znodes,//number of nodes for rectangular meshes
	Empty,//empty line '# (space)'
	Unknown,//unknown something after #
	Invalid //invalid syntaxis
};

//CAUTION important that matching is case-insensitive
//need to be careful about optimize flag too, can byte in the ass with few regexes inserted directly into fit function
constexpr auto rxFlags = std::regex_constants::icase | std::regex_constants::ECMAScript | std::regex_constants::optimize;

constexpr OVFParameter NODE_PARAMS[]{
	OVFParameter::Xnodes,
	OVFParameter::Ynodes,
	OVFParameter::Znodes
};

constexpr OVFParameter COORD_PARAMS[]{
	OVFParameter::Xbase,
	OVFParameter::Ybase,
	OVFParameter::Zbase,
	OVFParameter::Xstep,
	OVFParameter::Ystep,
	OVFParameter::Zstep
};

inline std::regex toRegex(const std::string& str)
{
	//pattern with first character always #
	//after arbitrary ammount of spaces it->second spaces : space optional value
	//and optional comment
	return std::regex("^#\\s*" + str + "\\s*:\\s*(.*)\\s*(#{2,}.*)?$", rxFlags);
}

const std::map<OVFParameter, std::regex> sPats
{
	{ OVFParameter::Open, toRegex("Begin") },
	{ OVFParameter::Close, toRegex("End") },
	{ OVFParameter::Comment, std::regex("^#{2,}(.*)$") },
	{ OVFParameter::Desc, toRegex("Desc") },
	{ OVFParameter::Munit, toRegex("meshunit") },
	{ OVFParameter::Segcnt, toRegex("Segment\\s+count") },
	{ OVFParameter::Munit, toRegex("meshunit") },
	{ OVFParameter::Vunit, toRegex("valueunits?") },//last s is optional for OVF 2
	{ OVFParameter::Vmult, toRegex("valuemultiplier") },
	{ OVFParameter::Vdim, toRegex("valuedim") },
	{ OVFParameter::Xmin, toRegex("xmin") },
	{ OVFParameter::Ymin, toRegex("ymin") },
	{ OVFParameter::Zmin, toRegex("zmin") },
	{ OVFParameter::Xmax, toRegex("xmax") },
	{ OVFParameter::Ymax, toRegex("ymax") },
	{ OVFParameter::Zmax, toRegex("zmax") },
	{ OVFParameter::Bound, toRegex("boundary") },
	{ OVFParameter::Vmax, toRegex("ValueRangeMaxMag") },
	{ OVFParameter::Vmin, toRegex("ValueRangeMinMax") },
	{ OVFParameter::Mtype, toRegex("meshtype") },
	{ OVFParameter::Pcount, toRegex("pointcount") },
	{ OVFParameter::Xbase, toRegex("xbase") },
	{ OVFParameter::Ybase, toRegex("ybase") },
	{ OVFParameter::Zbase, toRegex("zbase") },
	{ OVFParameter::Xstep, toRegex("xstepsize") },
	{ OVFParameter::Ystep, toRegex("ystepsize") },
	{ OVFParameter::Zstep, toRegex("zstepsize") },
	{ OVFParameter::Xnodes, toRegex("xnodes") },
	{ OVFParameter::Ynodes, toRegex("ynodes") },
	{ OVFParameter::Znodes, toRegex("znodes") },
	{ OVFParameter::Title, toRegex("title") },
	{ OVFParameter::Vlabels, toRegex("valuelabels") },
	{ OVFParameter::Empty, std::regex("^#\\s*") }
};

//Indexing types of different fields we expect
const std::map<OVFParameter, pType> paramIndex{
	{ OVFParameter::Open, pType::Other },
	{ OVFParameter::Close, pType::Other },
	{ OVFParameter::Comment, pType::Other },
	{ OVFParameter::Title, pType::Other },
	{ OVFParameter::Segcnt, pType::Other },//although returns uint, it is not related to header
	{ OVFParameter::Desc, pType::String },
	{ OVFParameter::Munit, pType::String },
	{ OVFParameter::Vunit, pType::String },
	{ OVFParameter::Vmult, pType::String },
	{ OVFParameter::Vdim, pType::Uint },
	{ OVFParameter::Xmin, pType::Float },
	{ OVFParameter::Ymin, pType::Float },
	{ OVFParameter::Zmin, pType::Float },
	{ OVFParameter::Xmax, pType::Float },
	{ OVFParameter::Ymax, pType::Float },
	{ OVFParameter::Zmax, pType::Float },
	{ OVFParameter::Bound, pType::Other },
	{ OVFParameter::Vmax, pType::Float },
	{ OVFParameter::Vmin, pType::Float },
	{ OVFParameter::Mtype, pType::Other },
	{ OVFParameter::Pcount, pType::Uint },
	{ OVFParameter::Xbase, pType::Float },
	{ OVFParameter::Ybase, pType::Float },
	{ OVFParameter::Zbase, pType::Float },
	{ OVFParameter::Xstep, pType::Float },
	{ OVFParameter::Ystep, pType::Float },
	{ OVFParameter::Zstep, pType::Float },
	{ OVFParameter::Xnodes, pType::Uint },
	{ OVFParameter::Ynodes, pType::Uint },
	{ OVFParameter::Znodes, pType::Uint },
	{ OVFParameter::Title, pType::String },
	{ OVFParameter::Vlabels, pType::String },
	{ OVFParameter::Empty, pType::Other },
	{ OVFParameter::Unknown, pType::Other },
	{ OVFParameter::Invalid, pType::Other }
};

//header for first version, example:
//# OOMMF: rectangular mesh v1.0
const std::regex OVF1Regex = std::regex("^#\\s*OOMMF\\s*:.*v1.0.*", rxFlags);

//header for second version, example:
//# OOMMF OVF 2.0
const std::regex OVF2Regex = std::regex("^#\\s*OOMMF\\s*OVF\\s*2.0.*", rxFlags);

//data token parser, examples:
//# Begin: data text
//# Begin: Data Binary 4
const std::regex DATARegex = std::regex("^data\\s+((?:binary)|(?:text))\\s+(\\d+)?.*", rxFlags);

//data ending parser
const std::regex dataEndRegex = std::regex("^End\\s*:\\s*Data\\s*(.*)\\s*(#{2,}.*)?", rxFlags);


bool readDouble(const std::string& str, double& val)
{
	try {
		val = std::stod(str);
		return true;
	}
	catch (const std::logic_error& err) {
		std::cerr << "Error trying to parse \'" << str << "\' as double, error was" << err.what() << std::endl;
		return false;
	}
	return true;
}

bool readUINT(const std::string& str, std::size_t& val)
{
	try {
		val = std::stoul(str);
		return true;
	}
	catch (const std::logic_error& err) {
		std::cerr << "Error trying to parse \'" << str << "\' as uint, error was" << err.what() << std::endl;
		return false;
	}
}

std::pair<OVFParameter, std::string> parseLine(const std::string& str)
{
	if (str[0] != '#')
		return { OVFParameter::Invalid, "" };

	std::smatch matches;
	for (auto it = sPats.begin(); it != sPats.end(); ++it)
		if (regex_match(str, matches, it->second))
			return { it->first, matches[1].str() }; //return the first matched subpattern, ignore the comment

	return { OVFParameter::Unknown, str };
}

//header segment
struct OVFHeader {
	std::map<OVFParameter, std::string> sParams{};
	std::map<OVFParameter, unsigned int> uParams{};
	std::map<OVFParameter, double> fParams{};
};

bool validateHeader(const struct OVFHeader& ref)
{
	auto is_rect = ref.uParams.find(OVFParameter::Mtype);
	//if rect type wasn't read we throw error!
	if (is_rect == ref.uParams.end())
		return false;
	bool rect = is_rect->second;

	if (!rect)
	{
		const auto& count = ref.uParams.find(OVFParameter::Pcount);
		if (count == ref.uParams.end())
			return false;
		else if (!count->second)
			return false;
	}
	else
	{
		std::size_t count = 1;
		for (const auto& x : NODE_PARAMS)
		{
			const auto& ncount = ref.uParams.find(x);
			if (ncount == ref.uParams.end())
				return false;
			else
				count *= ncount->second;
		}
		if (!count)
			return false;

		//check if parameters mandatory for getting coordinate were read
		for (const auto& x : COORD_PARAMS)
			if (ref.fParams.find(x) == ref.fParams.end())
				return false;
	}

	if (ref.uParams.find(OVFParameter::Vdim) == ref.uParams.end())
		return false;

	return true;
}


inline float flt_bswap(float x)
{
	auto rX = boost::endian::endian_reverse(*reinterpret_cast<uint32_t*>(&x));
	return *reinterpret_cast<float*>(&rX);
}

inline double dbl_bswap(double x)
{
	auto rX = boost::endian::endian_reverse(*reinterpret_cast<uint64_t*>(&x));
	return *reinterpret_cast<double*>(&rX);
}

std::vector<OVFSegment> readOVF(const std::string& path)
{
	//open file for binary read
	std::ifstream inFile(path, std::ios::binary);
	if (!inFile.is_open())
		throw  std::ios_base::failure("Weren't able to open file: \"" + path + "\"\n");
	std::string buffer = "";
	std::size_t lcntr = 0;//count lines in the file
	int version = -1;

	//flags for being inside of header or section
	bool inHeader = false;
	bool inSection = false;

	std::size_t scnt = 0;//number of segments in file
	std::size_t cur_smnt = 0;//current segment

	std::vector<OVFSegment> temp_storage{};

	//setup the parameter fields
	struct OVFHeader header;
	dataType sType = dataType::dataunk;
	void *data = nullptr;

	while (inFile.good())
	{

		//increase line counter before read
		++lcntr;
		std::getline(inFile, buffer);
		if (inFile.bad())
		{

			std::cerr << "File: \"" << path << "\"\n";
			std::cerr << "Unexpectedly got error reading, on line " << lcntr << "read \"" << buffer << "\"" << std::endl;
			goto DEINIT;
		}
		if (inFile.eof())
			break;
		//std::cout << "Line #" << lcntr << " Read: \"" <<buffer <<'\"'<< std::endl;

		//on first line should check the title
		//specified to always be the header
		if (lcntr == 1)
		{
			if (regex_match(buffer, OVF1Regex))
				version = 1;
			else if (regex_match(buffer, OVF2Regex))
				version = 2;
			else
				goto DEINIT;
			//if everything else fails, just say fuck it

			//setup the parameter fields
			struct OVFHeader header { {}, {}, {} };
			dataType sType = dataType::dataunk;
			void *data = nullptr;
			continue;
		}

		auto token = parseLine(buffer);
		switch (paramIndex.find(token.first)->second)
		{
		case pType::Float: {
			//Do not have business reading header values outside of header
			//no need to check for section since that is done in ptOther handler
			if (!inHeader)
			{
				std::cerr << "File: \"" << path << "\"\n";
				std::cerr << "Encountered floating point value out of header on line " << lcntr << " line: \"" << buffer << '\"' << std::endl;

				goto DEINIT;
			}
			double value;
			if (readDouble(token.second, value)) {
				//emplace the element and panic if cannot do it
				if (!header.fParams.emplace(token.first, value).second)
				{
					std::cerr << "File: \"" << path << "\"\n";
					std::cerr << "Encountered a repeating value in the same header on line " << lcntr << " line: \"" << buffer << "\"" << std::endl;

					goto DEINIT;
				}
			}
			else {
				std::cerr << "File: \"" << path << "\"\n";
				std::cerr << "Could not parse token \"" << token.second << "\" as double" << std::endl;

				goto DEINIT;
			}
			break;
		}
		case pType::Uint: {
			//Do not have business reading header values outside of header
			//no need to check for section since that is done in ptOther handler
			if (!inHeader)
			{
				std::cerr << "File: \"" << path << "\"\n";
				std::cerr << "Encountered uint point value out of header on line " << lcntr << " line: \"" << buffer << '\"' << std::endl;

				goto DEINIT;
			}
			std::size_t value;
			if (readUINT(token.second, value)) {
				//emplace the element and panic if cannot do it
				if (!header.uParams.emplace(token.first, value).second)
				{
					std::cerr << "File: \"" << path << "\"\n";
					std::cerr << "Encountered a repeating value in the same header on line " << lcntr << " line: \"" << buffer << "\"" << std::endl;

					goto DEINIT;
				}
			}
			else {
				std::cerr << "File: \"" << path << "\"\n";
				std::cerr << "Could not parse token \"" << token.second << "\" as double" << std::endl;

				goto DEINIT;
			}
			break;
		}
		case pType::String: {
			if (token.first != OVFParameter::Desc) {
				if (!header.sParams.emplace(token).second)
				{
					std::cerr << "File: \"" << path << "\"\n";
					std::cerr << "Encountered a repeating value in the same header on line " << lcntr << " line: \"" << buffer << "\"" << std::endl;

					goto DEINIT;
				}
			}
			else {
				//Description is allowed to be multiline!
				if(header.sParams.find(OVFParameter::Desc)!=header.sParams.end())
					header.sParams[OVFParameter::Desc] += "\n";
				header.sParams[OVFParameter::Desc] += token.second;
			}
			break;
		}
		case pType::Other: {
			switch (token.first)
			{
				//Handling for comments
			case OVFParameter::Comment: {
				//Do nothing for a comment
				break;
			}
										//Handling for segment count variable
			case OVFParameter::Segcnt: {
				if (scnt != 0)
				{
					std::cerr << "File: \"" << path << "\"\n";
					std::cerr << "Found second segcnt! On line: " << lcntr << std::endl;

					goto DEINIT;
				}
				if (inSection)
				{
					//in case the segcount is inside section
					std::cerr << "File: \"" << path << "\"\n";
					std::cerr << "Segment count found inside section!" << std::endl;

					goto DEINIT;
				}
				if (!readUINT(token.second, scnt))
				{
					std::cerr << "File: \"" << path << "\"\n";
					std::cerr << "Could not read value on line " << lcntr << " line: \"" << buffer << '\"' << std::endl;
					//in case we couldn't

					goto DEINIT;
				}
				if (scnt == 0)
				{
					std::cerr << "File: \"" << path << "\"\n";
					std::cerr << "Wrong count read for segment count, line: \"" << buffer << '\"' << std::endl;

					goto DEINIT;
				}
				break;
			}
			//Handling for meshtype variable
			case OVFParameter::Mtype: {
				std::size_t is_rect;
				if (std::regex_match(token.second, std::regex("rectangular", rxFlags))) {
					is_rect = 1;
				}
				else if (std::regex_match(token.second, std::regex("irregular", rxFlags))) {
					is_rect = 0;
				}
				else {
					//wrong parameter
					std::cerr << "File: \"" << path << "\"\n";
					std::cerr << "Wrong mesh type! line " << lcntr << " \"" << buffer << '\"' << std::endl;

					goto DEINIT;
				}
				if (!header.uParams.emplace(token.first, is_rect).second)
				{
					std::cerr << "File: \"" << path << "\"\n";
					std::cerr << "Encountered a repeating value in the same header on line " << lcntr << " line: \"" << buffer << "\"\n" << std::endl;

					goto DEINIT;
				}
				break;
			}
			//Handling for empty lines
			case OVFParameter::Empty: {
				std::cerr << "File: \"" << path << "\"\n";
				std::cerr << "WARNING: encountered empty line # " << lcntr << std::endl;
				break;
			}
			//Handling for unknown lines
			case OVFParameter::Unknown: {
				std::cerr << "File: \"" << path << "\"\n";
				std::cerr << "WARNING: encountered unknown line # " << "\"\n";
				std::cerr << "Line: \"" << buffer << '\"' << std::endl;
				break;
			}
			//Handling for invalid line
			case OVFParameter::Invalid: {
				std::cerr << "File: \"" << path << "\"\n";
				std::cerr << "FATAL: encountered invalid line # " << "\"\n";
				std::cerr << "Line: \"" << buffer << '\"' << std::endl;

				goto DEINIT;
			}
			//Handling for different closings
			case OVFParameter::Close: {
				if (std::regex_match(token.second, std::regex("segment", rxFlags))) {
					if (inSection)
						inSection = false;
					else {
						std::cerr << "File: \"" << path << "\"\n";
						std::cerr << "FATAL: unexpected end of segment on line #" << lcntr << std::endl;

						goto DEINIT;
					}
					break;
				}
				else if (std::regex_match(token.second, std::regex("header", rxFlags))) {
					if (inHeader)
						inHeader = false;
					else {
						std::cerr << "File: \"" << path << "\"\n";
						std::cerr << "FATAL: unexpected end of header on line #" << lcntr << std::endl;

						goto DEINIT;
					}
					break;
				}
				else {
					std::cerr << "File: \"" << path << "\"\n";
					std::cerr << "FATAL: unexpecting closing on line #" << lcntr << '\n';
					std::cerr << "Line: \"" << buffer << '\"' << std::endl;

					goto DEINIT;
				}
				break;
			}
			//Handling beginning of sections
			case OVFParameter::Open: {
				std::smatch matches;
				//Handling for beginning of segment
				if (std::regex_match(token.second, std::regex("segment", rxFlags)))
				{
					if (inSection)
					{
						std::cerr << "File: \"" << path << "\"\n";
						std::cerr << "FATAL: unexpecting beginning of section on line #" << lcntr << '\n';
						std::cerr << "Line: \"" << buffer << '\"' << std::endl;

						goto DEINIT;
					}
					if (cur_smnt > scnt)
					{
						std::cerr << "File: \"" << path << "\"\n";
						std::cerr << "FATAL: found more segments than expected" << std::endl;

						goto DEINIT;
					}
					//increase segment count and push new default segment
					cur_smnt++;
					inSection = true;
				}
				else if (std::regex_match(token.second, std::regex("header", rxFlags)))
				{
					if (!inSection || inHeader)
					{
						std::cerr << "File: \"" << path << "\"\n";
						std::cerr << "FATAL: unexpecting beginning of header on line #" << lcntr << '\n';
						std::cerr << "Line: \"" << buffer << '\"' << std::endl;

						goto DEINIT;
					}

					inHeader = true;
				}
				//else try matching pattern for data
				else if (std::regex_match(token.second, matches, DATARegex))
				{
					if (!validateHeader(header))
					{
						std::cerr << "File: \"" << path << "\"\n";
						std::cerr << "Invalid header for data:"<< buffer << std::endl;

						goto DEINIT;
					}

					std::size_t pntCount = 0;

					//calculate the point count in current segment
					if (header.uParams[OVFParameter::Mtype])//if rectangular
						pntCount = ((version == 2) ? header.uParams[OVFParameter::Vdim] : 3) *
						header.uParams[OVFParameter::Xnodes] * header.uParams[OVFParameter::Ynodes] * header.uParams[OVFParameter::Znodes];
					else//if irregular
						pntCount = ((version == 2) ? header.uParams[OVFParameter::Vdim] + 3 : 6) * header.uParams[OVFParameter::Pcount];


					if (std::regex_match(matches[1].str(), std::regex("text", rxFlags)))
					{
						//TODO implement text data reading
						//the easiest would be to simulate double array by reading into newly created array
						throw std::logic_error("Not implemented!");
					}
					//SHOULD not fail since pattern returns \d+
					if (matches.size() != 3)
					{
						//TODO tell about epic failure
						goto DEINIT;
					}

					switch (std::stoul(matches[2].str())) {
					case 4:
						sType = dataType::dataflt;
						break;
					case 8:
						sType = dataType::datadbl;
						break;
					default:
						std::cerr << "File: \"" << path << "\"\n";
						std::cerr << "Bad data descriptor: " << buffer << std::endl;
						goto DEINIT;
					}

					//allocate the memory
					if (sType == dataType::dataflt)
						data = reinterpret_cast<void*>(new float[pntCount]);
					else
						data = reinterpret_cast<void*>(new double[pntCount]);

					std::size_t dsize = pntCount * ((sType == dataType::datadbl) ? sizeof(double) : sizeof(float));

					// read and check the control number
					if (sType == dataType::dataflt) {
						float fltChk;
						inFile.read(reinterpret_cast<char*>(&fltChk), sizeof(float));
						if (!inFile.good())
						{/*TODO implement error throw*/
						}
						if (version == 2 && fltChk != FLT_TEST || version == 1 && fltChk != flt_bswap(FLT_TEST))
						{/*TODO implement error throw*/
						}
					}
					else {
						double dblChk;
						inFile.read(reinterpret_cast<char*>(&dblChk), sizeof(double));
						if (!inFile.good())
						{/*TODO implement error throw*/
						}
						if (version == 2 && dblChk != DBL_TEST || version == 1 && dblChk != dbl_bswap(DBL_TEST))
						{/*TODO implement error throw*/
						}
					}

					//read data
					//if anything std::size_t is good enough to address 1.84467e^7 Terabytes of data, not running out of that soon
					inFile.read(reinterpret_cast<char*>(data), dsize);
					if (!inFile.good())
					{/*TODO implement error throw*/
					}


					//bitswap all values if version is 1
					if (version == 1)
						for (std::size_t i = 0; i<pntCount; i++)
						{
							if (sType == dataType::dataflt)
								*(reinterpret_cast<float*>(data) + i) = flt_bswap(*(reinterpret_cast<float*>(data) + i));
							else
								*(reinterpret_cast<double*>(data) + i) = dbl_bswap(*(reinterpret_cast<double*>(data) + i));
						}


					OVFSegment cur;

					if (sType == dataType::dataflt) {
						VectorField<float> *field = new VectorField<float>();
						if (header.uParams[OVFParameter::Mtype])
							field->mv_init(reinterpret_cast<float*>(data),
								{ header.uParams[OVFParameter::Xnodes], header.uParams[OVFParameter::Ynodes], header.uParams[OVFParameter::Znodes] },
								{ header.fParams[OVFParameter::Xbase], header.fParams[OVFParameter::Ybase], header.fParams[OVFParameter::Zbase] },
								{ header.fParams[OVFParameter::Xstep],header.fParams[OVFParameter::Ystep],header.fParams[OVFParameter::Zstep] },
								header.uParams[OVFParameter::Vdim]);
						else
							field->mv_init(reinterpret_cast<float*>(data), header.uParams[OVFParameter::Pcount], header.uParams[OVFParameter::Vdim]);
						cur.init(field, dataType::dataflt);
						cur.descr = header.sParams[OVFParameter::Desc];
						cur.labels = header.sParams[OVFParameter::Munit];
					}
					else
					{
						VectorField<double> *field = new VectorField<double>();
						if (header.uParams[OVFParameter::Mtype])
							field->mv_init(reinterpret_cast<double*>(data),
								{ header.uParams[OVFParameter::Xnodes], header.uParams[OVFParameter::Ynodes], header.uParams[OVFParameter::Znodes] },
								{ header.fParams[OVFParameter::Xbase], header.fParams[OVFParameter::Ybase], header.fParams[OVFParameter::Zbase] },
								{ header.fParams[OVFParameter::Xstep],header.fParams[OVFParameter::Ystep],header.fParams[OVFParameter::Zstep] },
								header.uParams[OVFParameter::Vdim]);
						else
							field->mv_init(reinterpret_cast<double*>(data), header.uParams[OVFParameter::Pcount], header.uParams[OVFParameter::Vdim]);
						cur.init(field, dataType::datadbl);
						cur.descr = header.sParams[OVFParameter::Desc];
						cur.labels = header.sParams[OVFParameter::Munit];
					}
					temp_storage.push_back(std::move(cur));
					//TODO check the read data

					//TODO implement check of next line !
					std::getline(inFile, buffer);
					if (!inFile.good())
					{/*TODO implement error throw*/
					}

					if (!regex_match(buffer, matches, dataEndRegex)) {
						//TODO implement error throw
					}

				}
				break;
			}
			//Handling of something else
			default: {
				//WTF how can you reach here?
				break;
			}
			}
			//end switch(token.first)
		}
		//end case pType::Other
		}
		//end switch(paramIndex.find(token.first)->second)
		if (inHeader || inSection)
		{/*TODO implement error throw*/
		}
	}

	//plug in new data
	return temp_storage;

	//Deinitialization code
DEINIT:
	return {};
}
