#pragma once
//std libs should work on all C++11 systems
#include<string>
#include<vector>
#include<array>
#include<utility>
#include<functional>


//internal data type
enum class dataType {
	datadbl,//iec559 double
	dataflt,//iec559 float
	dataunk// not known yet
};


template<typename T>
class VectorField {
private:
	//array of data points in need to be re-indexed
	T * points{ nullptr };
	size_t arSize{ 0 };
	//type of data saved in array
	bool is_grid{ false };

	//Grid parameters
	//grid size
	size_t nx, ny, nz;
	//grid step
	double dx, dy, dz;
	//grid base
	double x0, y0, z0;

	//number of datapoints
	size_t count;
	//dimension of vector field points
	size_t dim;
	
	//convert index to coordinate
	inline std::array<T, 3> deIndex(const size_t& x, const size_t& y, const size_t& z);
public:

	//define a good datatype for internal use
	typedef  std::pair<std::array<T, 3>, std::vector<T>> CPair;

	//block of constructors and such
	VectorField();
	VectorField(const VectorField& ref);
	VectorField(VectorField&& rref);
	~VectorField();
	VectorField& operator=(const VectorField& ref);
	VectorField& operator=(VectorField&& rref);

	//cast a type
	template<typename U>
	VectorField<U> cast();

	//initialize the array using c-style inputs
	//non-grid style
	VectorField& init(const T* const data, const size_t& count, const size_t& dim);
	//no copy version
	VectorField& mv_init(T* data, const size_t& count, const size_t& dim);
	//grid style
	VectorField& init(const T* const data, const std::array<size_t, 3>& size,
		const std::array<double, 3>& base, const std::array<double, 3>& step, const size_t& _dim = 3);
	//no copy
	VectorField& mv_init(T* data, const std::array<size_t, 3>& size,
		const std::array<double, 3>& base, const std::array<double, 3>& step, const size_t& _dim = 3);

	//access an element
	CPair getPoint(const size_t& x, const size_t& y, const size_t& z);

	//get a slice of array
	std::vector<CPair> select(std::function<bool(const std::array<T, 3>&, const std::vector<T>&)> func);
	//select on a layer
	std::vector<CPair> selectInLayer(std::function<bool(const std::array<T, 3>&, const std::vector<T>&)> func, const size_t& zlayer);
	//get grid dimensions
	std::array<std::size_t, 3> gridDimensions()
	{
		if(is_grid)
			return {nx, ny, nz};
		else
			return {};
	}
};

//vector field class implementations
template<typename T>
VectorField<T>::VectorField()
{
	//All the important values are already set-up
}

//only data array needs deleting
template<typename T>
VectorField<T>::~VectorField()
{
	if(points!=nullptr)
		delete[] points;
}



//non grid init
template<typename T>
VectorField<T>& VectorField<T>::init(const T* const data, const size_t& _count, const size_t& _dim)
{
	//since this method got called this is not a grid
	is_grid = false;

	dim = _dim;
	count = _count;

	//TODO: handle the case of dim <1

	
	//the total count is assumed to be having 3+dim values per point
	arSize = (3 + dim) * count;

	//copy the data
	points = new T[arSize];
	for (size_t i = 0; i < arSize; i++)
		points[i] = data[i];

	return *this;
}
template<typename T>
VectorField<T>& VectorField<T>::mv_init(T* data, const size_t& _count, const size_t& _dim)
{
	//since this method got called this is not a grid
	is_grid = false;

	dim = _dim;
	count = _count;

	//TODO: handle the case of dim <1

	this->dim;
	//the total count is assumed to be having 3+dim values per point
	arSize = (3 + dim) * count;

	//move the data
	points = data;

	return *this;
}

//grid init
template<typename T>
VectorField<T>& VectorField<T>::init(const T* const data, const std::array<size_t, 3>& size,
	const std::array<double, 3>& base, const std::array<double, 3>& step, const size_t& _dim)
{
	is_grid = true;
	dim = _dim;
	//TODO validate the numbers
	//init grid params
	nx = size[0]; ny = size[1]; nz = size[2];
	dx = step[0]; dy = step[1]; dz = step[2];
	x0 = base[0]; y0 = base[1]; z0 = base[2];
	//number of points is ez
	arSize = dim * nx * ny * nz;
	//point count is still useful
	count = nx * ny * nz;

	points = new T[arSize];
	for (size_t i = 0; i < arSize; i++)
		points[i] = data[i];

	return *this;
}

template<typename T>
VectorField<T>& VectorField<T>::mv_init(T* data, const std::array<size_t, 3>& size,
	const std::array<double, 3>& base, const std::array<double, 3>& step, const size_t& _dim)
{
	is_grid = true;
	dim = _dim;
	//TODO validate the numbers
	//init grid params
	nx = size[0]; ny = size[1]; nz = size[2];
	dx = step[0]; dy = step[1]; dz = step[2];
	x0 = base[0]; y0 = base[1]; z0 = base[2];
	//number of points is ez
	arSize = dim * nx * ny * nz;
	//point count is still useful
	count = nx * ny * nz;

	points = data;

	return *this;
}

//funky move stuff
template<typename T>
VectorField<T>::VectorField(VectorField&& rref) :
	is_grid(rref.is_grid), arSize(rref.arSize), dim(rref.dim), count(rref.count)
{
	//std::cout << "move constructor" << std::endl;
	//if already had data, clean it up
	if (points != nullptr)
		delete[] points;

	//in case grid is rectangular copy the relevant info
	if (is_grid)
	{
		dx = rref.dx; dy = rref.dy; dz = rref.dz;
		nx = rref.nx; ny = rref.ny; nz = rref.nz;
		x0 = rref.x0; y0 = rref.y0; z0 = rref.z0;
	}

	points = rref.points;
	rref.points = nullptr;
}

template<typename T>
VectorField<T>::VectorField(const VectorField& ref) :
	is_grid(ref.is_grid), arSize(ref.arSize), dim(ref.dim), count(ref.count)
{
	//std::cout << "copy constructor" << std::endl;
	if (points != nullptr)
		delete[] points;
	//in case grid is rectangular copy the relevant info
	if (is_grid)
	{
		dx = ref.dx; dy = ref.dy; dz = ref.dz;
		nx = ref.nx; ny = ref.ny; nz = ref.nz;
		x0 = ref.x0; y0 = ref.y0; z0 = ref.z0;
	}

	//std::cout << arSize;
	points = new T[arSize];
	//copy data over to new place
	for (size_t i = 0; i < arSize; i++)
		points[i] = ref.points[i];
}

template<typename T>
VectorField<T>& VectorField<T>::operator=(const VectorField& ref)
{
	//std::cout << "copy assignment" << std::endl;
	if (points != nullptr)
		delete[] points;
	is_grid = ref.is_grid;
	arSize = ref.arSize;
	dim = ref.dim;
	count = ref.count;

	//in case grid is rectangular copy the relevant info
	if (is_grid)
	{
		dx = ref.dx; dy = ref.dy; dz = ref.dz;
		nx = ref.nx; ny = ref.ny; nz = ref.nz;
		x0 = ref.x0; y0 = ref.y0; z0 = ref.z0;
	}

	points = new T[arSize];
	//in case target container is not empty copy its contents
	if (ref.points != nullptr)
	{
		points = new T[arSize];
		//copy data over to new place
		for (size_t i = 0; i < arSize; i++)
			points[i] = ref.points[i];
	}
	else
		points = nullptr;
	
	return *this;
}



//move assignment
template<typename T>
VectorField<T>& VectorField<T>::operator=(VectorField&& rref)
{
	//std::cout << "move assignment" << std::endl;
	if (points != nullptr)
		delete[] points;
	is_grid = rref.is_grid;
	arSize = rref.arSize;
	dim = rref.dim;
	count = rref.count;

	//in case grid is rectangular copy the relevant info
	if (is_grid)
	{
		dx = rref.dx; dy = rref.dy; dz = rref.dz;
		nx = rref.nx; ny = rref.ny; nz = rref.nz;
		x0 = rref.x0; y0 = rref.y0; z0 = rref.z0;
	}

	//and move the array too
	points = rref.points;
	rref.points = nullptr;
}



//turn index into coordinate
template<typename T>
inline std::array<T, 3> VectorField<T>::deIndex(const size_t& x, const size_t& y, const size_t& z)
{
	if (!is_grid)
		throw std::invalid_argument("Trying to index unordered grid");

	if (x >= nx || y >= ny || z >= nz)
		throw std::out_of_range("Trying to access outside the array");

	std::array<T, 3> X{ x0 + x * dx, y0 + y * dy, z0 + z * dz };
	return std::move(X);
}

//get a point by index, only for grids
template<typename T>
std::pair<std::array<T, 3>, std::vector<T>> VectorField<T>::getPoint(const size_t& x, const size_t& y, const size_t& z)
{
	if (points == nullptr)
		throw std::logic_error("using unitialized array");
	auto base = dim * (x + nx * y + nx * ny * z);
	std::vector<T> Y{};
	for (size_t i = 0; i < dim; i++)
		Y.push_back(points[i + base]);


	return { deIndex(x, y, z), Y };
}


//cast vector field to different type of vector components]
template<typename T>
template<typename U>
VectorField<U> VectorField<T>::cast()
{
	VectorField<U> res;
	if (points == nullptr)
		return std::move(res);

	U* dCopy = new U[arSize];
	for (size_t i = 0; i < arSize; i++)
		dCopy[i] = static_cast<U>(points[i]);

	if (is_grid)
		res.mv_init(dCopy, { nx,ny,nz }, { x0,y0,z0 }, { dx,dy,dz }, dim);
	else
		res.mv_init(dCopy, count, dim);

	return std::move(res);
}


template<typename T>
std::vector<std::pair<std::array<T, 3>, std::vector<T>>> VectorField<T>::select(std::function<bool(const std::array<T, 3>&, const std::vector<T>&)> func)
{
	std::vector<CPair> collector{};
	if (is_grid)
	{
		for (size_t i = 0; i < nx; i++)
			for (size_t j = 0; j < ny; j++)
				for (size_t k = 0; k < nz; k++) {
					auto pnt = getPoint(i, j, k);
					if (func(pnt.first, pnt.second))
						collector.push_back(std::move(pnt));
				}
	}
	else
	{
		std::array<T, 3> xPoint;
		std::vector<T> yPoint;
		for (size_t i = 0; i < count; i++)
		{
			yPoint.empty();
			for (size_t j = 0; j < arSize; j++)
				xPoint[j] = points[i*dim + j];
			for (size_t j = 0; j < dim; j++)
				yPoint.push_back(points[3 + i * dim + j]);
		}
	}
	return std::move(collector);
}

template<typename T>
std::vector<std::pair<std::array<T, 3>, std::vector<T>>> VectorField<T>::selectInLayer(std::function<bool(const std::array<T, 3>&, const std::vector<T>&)> func, const size_t& zlayer)
{
	if (!is_grid)
		throw std::out_of_range("Cannot index irregular arrays");

	std::vector<CPair> collector{};
	for (size_t i = 0; i<nx; i++)
		for (size_t j = 0; j<ny; j++) {
			//std::cout << "b4 getPoint "<<i<<" "<<j<<std::endl;
			auto pnt{ getPoint(i, j, zlayer) };
			if (func(pnt.first, pnt.second))
			{
				//std::cout << "B4 push into collector" << std::endl;
				collector.push_back(pnt);
			}
		}
	return collector;
}


//container
class OVFSegment {
	//Vector field pointer
	void* vf{ nullptr };
	dataType type{ dataType::dataunk };
public:
	std::string descr{ "" }, labels{ "" };
	template<typename T>
	VectorField<T> cast(){
		if (type == dataType::dataflt)
			return reinterpret_cast<VectorField<float>*>(vf)->cast<T>();
		else if (type == dataType::datadbl)
			return reinterpret_cast<VectorField<double>*>(vf)->cast<T>();
		else
			throw std::logic_error("Not implemented types apart from float and double");
	}
	~OVFSegment()
	{
		if (vf == nullptr)
			return;
		if (type == dataType::dataflt)
		{
			delete reinterpret_cast<VectorField<float>*>(vf);
			return;
		}
		else if (type == dataType::datadbl)
		{
			delete reinterpret_cast<VectorField<double>*>(vf);
			return;
		}
	}
	OVFSegment() {}
	void clear()
	{
		if (vf != nullptr)
			if(type == dataType::dataflt)
				delete[] reinterpret_cast<float*>(vf);
			else if(type == dataType::datadbl)
				delete[] reinterpret_cast<double*>(vf);
		type = dataType::dataunk;
	}
	inline void init(void *vfield, dataType _type)
	{
		clear();
		type = _type;
		vf = vfield;
	}
	template<typename T>
	void init(const VectorField<T>& ref)
	{
		void* vref = reinterpret_cast<void*>(new VectorField<T>(ref));
		if (std::is_same<T, float>::value)
			init(vref, dataType::dataflt);
		else if (std::is_same<T, double>::value)
			init(vref, dataType::datadbl);
		else
			throw std::logic_error("Calling unimplemented type initialization");
	}
	dataType currentType()
	{
		return type;
	}
	OVFSegment(const OVFSegment& ref):
		type(ref.type), descr(ref.descr), labels(ref.labels)
	{
		if (ref.vf == nullptr)
			return;
		if (type == dataType::dataflt)
			vf = new VectorField<float>(*reinterpret_cast<VectorField<float>*>(ref.vf));
		else if (type == dataType::datadbl)
			vf = new VectorField<double>(*reinterpret_cast<VectorField<double>*>(ref.vf));
		else
			throw std::logic_error("Not implemented types apart from float and double");
	}
	OVFSegment(OVFSegment&& rref):
		type(rref.type), descr(rref.descr), labels(rref.labels)
	{
		vf = rref.vf;
		rref.vf = nullptr;
	}
};

std::vector<OVFSegment> readOVF(const std::string& path);
