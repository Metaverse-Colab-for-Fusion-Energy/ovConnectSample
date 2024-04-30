# Connect Samples for the Omniverse Client Library

Build your own NVIDIA Omniverse Connector by following our samples that use Pixar USD and Omniverse Client Library APIs:

- Omni Asset Validator - A command line validation tool.
- Omni CLI - A command line utility to manage files on a Nucleus server.
- HelloWorld (C++ and Python) - A sample program that shows how to connect to an Omniverse Nucleus server, create a USD stage, create a polygonal box, bind a material, add a light, save data to .usd file, create and edit a .live layer, and send/receive messages over a channel on Nucleus. This sample is provided in both C++ and Python to demonstrate the Omniverse APIs for each language.
- LiveSession (C++ and Python) - A sample program that demonstrates how to create, join, merge, and participate in live sessions. This sample is provided in both C++ and Python to demonstrate the Omniverse APIs for each language.
- OmniUsdaWatcher (C++) - A live USD watcher that outputs a constantly updating USDA file on disk.
- OmniSimpleSensor (C++) - A C++ program that demonstrates how to connect external input (e.g sensor data) to a USD layer in Nucleus.

## Using the prebuilt package from the Omniverse Launcher

If the Connect Sample was downloaded from the Omniverse Launcher then the sample programs are already built and can be run with the relevant `run_*.bat` or `run_*.sh` commands. They all accept commandline arguments are are best experienced from an interactive terminal.

If you are interested in building these samples yourself, proceed to the [`How to build`](#how-to-build) section to download all of the build dependencies and build the samples.

## How to build

### Linux
This project requires "make" and "g++".

- Open a terminal.
- To obtain "make" type ```sudo apt install make``` (Ubuntu/Debian), or ```yum install make``` (CentOS/RHEL).  
- For "g++" type ```sudo apt install g++``` (Ubuntu/Debian), or ```yum install gcc-c++``` (CentOS/RHEL).

Use the provided build script to download all other dependencies (e.g USD), create the Makefiles, and compile the code.

```bash
./repo.sh build
```

Use any of the `run_*.sh` scripts (e.g. `./run_hello_world.sh`) to execute each program with a pre-configured environment.

> Tip: If you prefer to manage the environment yourself, add `<samplesRoot>/_build/linux64-x86_64/release` to your `LD_LIBRARY_PATH`.

For commandline argument help, use `--help`
```bash
./run_hello_world.sh --help
```

> Note : For omnicli, use `./omnicli.sh help` instead.

### Windows
#### Building
Use the provided build script to download all dependencies (e.g USD), create the projects, and compile the code.  
```bash
.\repo.bat build
```

Use any of the `run_*.bat` scripts (e.g. `.\run_hello_world.bat`) to execute each program with a pre-configured environment.

For commandline argument help, use `--help`
```bash
.\run_hello_world.bat --help
```

> Note : For omnicli, use `.\omnicli.bat help` instead.

#### Building within the Visual Studio IDE

To build within the VS IDE, open `_compiler\vs2019\Samples.sln` in Visual Studio 2019.  The sample C++ code can then be tweaked, debugged, rebuilt, etc. from there.  

> Note : If the Launcher installs the Connect Samples into the `%LOCALAPPDATA%` folder, Visual Studio will not "Build" properly when changes are made because there is something wrong with picking up source changes.  Do one of these things to address the issue:
>  - `Rebuild` the project with every source change rather than `Build`
>  - Copy the Connect Samples folder into another folder outside of `%LOCALAPPDATA%`
>  - Make a junction to a folder outside of %LOCALAPPDATA% and open the solution from there:
>    - `mklink /J C:\connect-samples %LOCALAPPDATA%\ov\pkg\connectsample-202.0.0`

#### Changing the MSVC Compiler [Advanced]

When `repo.bat build` is run, a version of the Microsoft Visual Studio Compiler and the Windows 10 SDK are downloaded and referenced by the generated Visual Studio projects.  If a user wants the projects to use an installed version of Visual Studio 2019 then run `repo.bat build --use-devenv`.  Note, the build scripts are configured to tell premake to generate VS 2019 project files.  Some plumbing is required to support other Visual Studio versions.


## Issues with Self-Signed Certs
If the scripts from the Connect Sample fail due to self-signed cert issues, a possible workaround would be to do this:

Install python-certifi-win32 which allows the windows certificate store to be used for TLS/SSL requests:

```bash
%PM_PYTHON% -m pip install python-certifi-win32 --trusted-host pypi.org --trusted-host files.pythonhosted.org
```

Note the %PM_PYTHON% is an environment variable set by the build script.



## Documentation and learning resources for USD and Omniverse

[USD Docs - Creating Your First USD Stage](https://graphics.pixar.com/usd/docs/Hello-World---Creating-Your-First-USD-Stage.html)

[Pixar USD API Docs](https://graphics.pixar.com/usd/docs/api/index.html)

[Pixar USD User Docs](https://graphics.pixar.com/usd/release/index.html)

[NVDIDA USD Docs](https://developer.nvidia.com/usd)

[Omniverse Client Library API Docs](https://omniverse-docs.s3-website-us-east-1.amazonaws.com/client_library)

[Omniverse USD Resolver API Docs](http://omniverse-docs.s3-website-us-east-1.amazonaws.com/usd_resolver)