#include<wstp.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h> 
#include<boost/filesystem.hpp>
#include"OVFReader.h"

//the main loop from mathematica
#if WINDOWS_MATHLINK

#if __BORLANDC__
#pragma argsused
#endif

int PASCAL WinMain(HINSTANCE hinstCurrent, HINSTANCE hinstPrevious, LPSTR lpszCmdLine, int nCmdShow)
{
	//Beep(400, 500);
	char  buff[512];
	char FAR * buff_start = buff;
	char FAR * argv[32];
	char FAR * FAR * argv_end = argv + 32;

	hinstPrevious = hinstPrevious; /* suppress warning */

	if (!WSInitializeIcon(hinstCurrent, nCmdShow)) return 1;
	WSScanString(argv, &argv_end, &lpszCmdLine, &buff_start);
	return WSMain((int)(argv_end - argv), argv);
}

#else

int main(int argc, char* argv[])
{
	return WSMain(argc, argv);
}

#endif

//Fatal error macro
inline void GenerateFatalError(const char* error)
{
	WSPutFunction(stdlink, "CompoundExpression", 2);
	WSPutFunction(stdlink, "Message", 1);
	WSPutFunction(stdlink, "MessageName", 2);
	WSPutSymbol(stdlink, "ImportOVF");
	WSPutString(stdlink, error);
	WSPutSymbol(stdlink, "$Failed");
	WSEndPacket(stdlink);
	WSFlush(stdlink);
}

extern "C" void import(const char *, int *, long);

//importer function 
void import(const char *fName, int *layers, long layCount)
{
	//Check if the file exists
	if(!boost::filesystem::exists(fName))
	{
		GenerateFatalError("notfound");
		return;
	}
	
	std::vector<size_t> Layers{};
	for(std::size_t i = 0; i < layCount; i++)
		Layers.push_back(layers[i] - 1);
	
	//ovf storage vector
	std::vector<OVFSegment> data{};
	//Read the file
	try{
		data = readOVF(fName);
	}catch(std::exception& e){
		GenerateFatalError("badformat");
		return;
	}
	
	//output the damn thing
	WSPutFunction(stdlink, "List", data.size());
	for(auto& seg: data)
	{
		auto vField = seg.cast<double>();
		auto gridSize = vField.gridDimensions();
		//if segment is non-rectangular it is real bad
		if(gridSize.size() == 0)
		{
			GenerateFatalError("badformat");
			goto DEINIT;
		}
		
		std::vector<size_t> impLayers{};
		//if no layers were provided export fucking everything
		if( layCount == 0)
			for(int i = 0; i < gridSize[2]; i++)
				impLayers.push_back(i);
		else
			impLayers = Layers;
		
		//list of layers
		WSPutFunction(stdlink, "List", impLayers.size());
		for(const auto& l : impLayers)
		{
			if( l >= gridSize[2] || l < 0)
			{
				GenerateFatalError("badformat");
				goto DEINIT;
			}
			
			WSPutFunction(stdlink, "List", gridSize[1]);
			for(std::size_t j = 0; j < gridSize[1]; j++)
			{
				WSPutFunction(stdlink, "List", gridSize[0]);
				for(std::size_t i = 0; i < gridSize[0]; i++)
				{
					const auto& vec = vField.getPoint(i, j, l);
					
					WSPutFunction(stdlink, "List", 2);
					//coordinate
					WSPutFunction(stdlink, "List",3);
					for(const auto& x : vec.first)
						WSPutReal64(stdlink, x);
					
					//value
					WSPutFunction(stdlink, "List",3);
					for(const auto& x : vec.second)
						WSPutReal64(stdlink, x);
				}
			}
		}
	}
	
DEINIT:
	//flush the buffers
	WSEndPacket(stdlink);
	WSFlush(stdlink);
	return;
}
