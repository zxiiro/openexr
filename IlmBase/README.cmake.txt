WINDOWS
-------

Build IlmBase and OpenEXR on Windows using cmake
------------------

What follows are instructions for generating Visual Studio solution 
files and building those two packages

1. Launch a command window, navigate to the IlmBase folder with 
CMakeLists.txt,and type command:

	setlocal
	del /f CMakeCache.txt
        cmake  -DCMAKE_INSTALL_PREFIX=<where you want to instal the openexr builds>  -G "Visual Studio 10 Win64" ..\ilmbase


2. Navigate to IlmBase folder in Windows Explorer, open ILMBase.sln
and build the solution. When it build successfully, right click 
INSTALL project and build. It will install the output to the path
you set up at the previous step.  

3. Go to http://www.zlib.net and download zlib 
	  
4. Launch a command window, navigate to the OpenEXR folder with 
CMakeLists.txt, and type command:	  

        export PREFIX=/path/to/ilmbase/install/prefix
        export LD_LIBRARY_PATH=$PREFIX/lib
        export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig

	setlocal
	del /f CMakeCache.txt
	cmake 
      -DZLIB_ROOT=<zlib location>
      -DILMBASE_PACKAGE_PREFIX=<where you installed the ilmbase builds>
      -DCMAKE_INSTALL_PREFIX=<where you want to instal the openexr builds>
      -G "Visual Studio 10 Win64" ^
      ..\openexr

5. Navigate to OpenEXR folder in Windows Explorer, open OpenEXR.sln
and build the solution. When it build successfully, right click 
INSTALL project and build. It will install the output to the path
you set up at the previous step. 


6. For PyIlmbase, be sure to add the following to the environment:

    export PYTHONPATH=$PREFIX/lib/python2.[6,7]/site-packages
    
    

LINUX
-----
mkdir  /tmp/openexrbuild
cd     /tmp/openexrbuild

-------------
-- IlmBase --
-------------
initial bootstraping:
    cmake -DCMAKE_INSTALL_PREFIX=<install location> <source location of IlmBase>

build the actual code base:
    make -j 4

for testing do:
    make test

then to install to your chosen location:
    make install



Notes:

  make help # display the available make targets
  make # run make VERBOSE=1 to see the debug output
  make install
  make test # run the tests, IexTest, ImathTest, etc... IlmImfFuzzTest and IlmImfTest take a __long__ time
  make package_source # build a source tarball


