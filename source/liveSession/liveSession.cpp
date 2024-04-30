/*###############################################################################
#
# Copyright 2020 NVIDIA Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of
# this software and associated documentation files (the "Software"), to deal in
# the Software without restriction, including without limitation the rights to
# use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
# the Software, and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
# FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
###############################################################################*/

/*###############################################################################
#
# This liveSession sample demonstrates how to connect to a live session using
# the non-destructive live workflow.  A .live layer is used in the stage's session
# layer to contain the changes.  An Omniverse channel is used to broadcast users
# and merge notifications to all clients, and a session config (TOML) file is used
# to determine the "owner" of the session.
#
# * Initialize the Omniverse Resolver Plugin
# * Display existing live sessions for a stage
# * Connect to a live session
# * Set the edit target to the .live layer so changes replicate to other clients
# * Make xform changes to a mesh prim in the .live layer
# * Rename a prim that exists in the .live layer
# * Display the owner of the live session
# * Display the current connected users/peers in the session
# * Emit a GetUsers message to the session channel
# * Display the contents of the session config
# * Merge the changes from the .live session back to the root stage
# * Respond (by exiting) when another user merges session changes back to the root stage
#
###############################################################################*/

#include "xformUtils.h"
#include "primUtils.h"

#include <chrono>
#include <cctype>
#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <vector>
#include <cassert>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#ifdef _WIN32
    #include <conio.h>
    #include <windows.h>
    #include <filesystem>
    namespace fs = std::filesystem;
#else
    #include <limits.h>
    #include <unistd.h>     //readlink
    #include <experimental/filesystem>
    namespace fs = std::experimental::filesystem;
#endif

#include <fstream>
#include <mutex>
#include <memory>
#include <map>
#include <condition_variable>

#include "OmniClient.h"
#include "OmniUsdResolver.h"
#include "OmniChannel.h"
#include "LiveSessionInfo.h"
#include "LiveSessionConfigFile.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/metrics.h"
#include <pxr/base/gf/matrix4f.h>
#include "pxr/base/gf/vec2f.h"
#include <pxr/base/plug/registry.h>
#include <pxr/base/tf/stringUtils.h>
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usdGeom/primvar.h"
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usd/modelAPI.h>

PXR_NAMESPACE_USING_DIRECTIVE

// Globals for Omniverse Connection and base Stage
static UsdStageRefPtr gStage;
static LiveSessionInfo gLiveSessionInfo;
static OmniChannel gOmniChannel;
static const char *gAppName = "C++ Connect Sample";
static bool gStageMerged = false;

// Omniverse logging is noisy, only enable it if verbose mode (-v)
static bool gOmniverseLoggingEnabled = false;

// Global for making the logging reasonable
static std::mutex gLogMutex;

static GfVec3d gDefaultRotation(0);
static GfVec3i gDefaultRotationOrder(0, 1, 2);
static GfVec3d gDefaultScale(1);

static void OmniClientConnectionStatusCallbackImpl(void* userData, const char* url, OmniClientConnectionStatus status) noexcept
{
    // Let's just print this regardless
    {
        std::unique_lock<std::mutex> lk(gLogMutex);
        std::cout << "Connection Status: " << omniClientGetConnectionStatusString(status) << " [" << url << "]" << std::endl;
    }
    if (status == eOmniClientConnectionStatus_ConnectError)
    {
        // We shouldn't just exit here - we should clean up a bit, but we're going to do it anyway
        std::cout << "[ERROR] Failed connection, exiting." << std::endl;
        exit(-1);
    }
}

static void failNotify(const char *msg, const char *detail = nullptr, ...)
{
    std::unique_lock<std::mutex> lk(gLogMutex);

    fprintf(stderr, "%s\n", msg);
    if (detail != nullptr)
    {
        fprintf(stderr, "%s\n", detail);
    }
}

// Shut down Omniverse connection
static void shutdownOmniverse()
{
    // Calling this prior to shutdown ensures that all pending live updates complete.
    omniClientLiveWaitForPendingUpdates();

    // The stage is a sophisticated object that needs to be destroyed properly.  
    // Since gStage is a smart pointer we can just reset it
    gStage.Reset();

    omniClientShutdown();
}

// Omniverse Log callback
static void logCallback(const char* threadName, const char* component, OmniClientLogLevel level, const char* message) noexcept
{
    std::unique_lock<std::mutex> lk(gLogMutex);
    if (gOmniverseLoggingEnabled)
    {
        puts(message);
    }
}

// Get the Absolute path of the current executable
// Borrowed from https://stackoverflow.com/questions/1528298/get-path-of-executable
static fs::path getExePath()
{
#ifdef _WIN32
    wchar_t path[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, path, MAX_PATH);
    return path;
#else
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    return std::string(result, (count > 0) ? count : 0);
#endif
}

// Startup Omniverse 
static bool startOmniverse()
{
    // This is not strictly required for this sample because the sample copies all of the USD plugin
    // files to the correct place relative to the executable and current working directory.  This is
    // an instructional bit for apps that may not be able to do this.

    // Find absolute path of the resolver plugins `resources` folder
    std::string pluginResourcesFolder = getExePath().parent_path().string() + "/usd/omniverse/resources";
    PlugRegistry::GetInstance().RegisterPlugins(pluginResourcesFolder);
    std::string PluginName = "OmniUsdResolver";
    if (TfType::FindByName(PluginName).IsUnknown())
    {
        failNotify("Could not find the Omniverse USD Resolver plugin");
        return false;
    }

    // Register a function to be called whenever the library wants to print something to a log
    omniClientSetLogCallback(logCallback);

    // The default log level is "Info", set it to "Debug" to see all messages
    omniClientSetLogLevel(eOmniClientLogLevel_Debug);

    // Initialize the library and pass it the version constant defined in OmniClient.h
    // This allows the library to verify it was built with a compatible version. It will
    // return false if there is a version mismatch.
    if (!omniClientInitialize(kOmniClientVersion))
    {
        return false;
    }

    omniClientRegisterConnectionStatusCallback(nullptr, OmniClientConnectionStatusCallbackImpl);

    return true;
}

// This function will add a commented checkpoint to a file on Nucleus if:
//   The Nucleus server supports checkpoints
static void checkpointFile(const std::string& stageUrl, const char* comment, bool bForce = true)
{
    bool bCheckpointsSupported = false;
    omniClientWait(omniClientGetServerInfo(stageUrl.c_str(), &bCheckpointsSupported, 
        [](void* UserData, OmniClientResult Result, OmniClientServerInfo const * Info) noexcept
        {
            if (Result == eOmniClientResult_Ok && Info && UserData)
            {
                bool* bCheckpointsSupported = static_cast<bool*>(UserData);
                *bCheckpointsSupported = Info->checkpointsEnabled;
            }
        }));

    if (bCheckpointsSupported)
    {
        omniClientWait(omniClientCreateCheckpoint(stageUrl.c_str(), comment, bForce, nullptr, 
        [](void* userData, OmniClientResult result, char const * checkpointQuery) noexcept
        {}));

        std::unique_lock<std::mutex> lk(gLogMutex);
        std::cout << "Adding checkpoint comment <" << comment << "> to stage <" << stageUrl <<">" << std::endl;
    }
}

static std::string getConnectedUsername(const char* stageUrl)
{
    // Get the username for the connection
    std::string userName("_none_");
    omniClientWait(omniClientGetServerInfo(stageUrl, &userName, [](void* userData, OmniClientResult result, struct OmniClientServerInfo const * info) noexcept
        {
            std::string* userName = static_cast<std::string*>(userData);
            if (userData && userName && info && info->username)
            {
                userName->assign(info->username);
            }
        }));

    return userName;
}

// Opens an existing stage and finds the first UsdGeomMesh
static UsdGeomMesh findGeomMesh(const std::string& existingStage)
{
    // Open this file from Omniverse
    gStage = UsdStage::Open(existingStage);
    if (!gStage)
    {
        failNotify("Failure to open stage in Omniverse:", existingStage.c_str());
        exit(1);
        return UsdGeomMesh();
    }

    {
        std::unique_lock<std::mutex> lk(gLogMutex);
        std::cout << "Existing stage opened: " << existingStage << std::endl;
    }

    // Traverse the stage and return the first UsdGeomMesh we find
    auto range = gStage->Traverse();
    for (const auto& node : range)
    {
        //std::cout << "Node: " << node.GetName() << std::endl;
        if (node.IsA<UsdGeomMesh>())
        {
            {
                std::unique_lock<std::mutex> lk(gLogMutex);
                std::cout << "Found UsdGeomMesh: " << node.GetName() << std::endl;
            }
            return UsdGeomMesh(node);
        }
    }

    // No UsdGeomMesh found in stage (what kind of stage is this anyway!?)
    std::cout << "ERROR: No UsdGeomMesh found in stage: " << existingStage << std::endl;
    return UsdGeomMesh();
}

void findOrCreateSession(UsdStageRefPtr rootStage, LiveSessionInfo& liveSessionInfo)
{
    UsdStageRefPtr liveStage;
    std::string sessionFolderForStage = liveSessionInfo.GetSessionFolderPathForStage();
    std::vector<std::string> sessionList = liveSessionInfo.GetLiveSessionList();
    std::cout << "Select or create a Live Session:" << std::endl;
    for (int i = 0; i < sessionList.size(); i++)
    {
        std::cout << " [" << i << "] " << sessionList[i] << std::endl;
    }
    std::cout << " [n] Create a new session" << std::endl;
    std::cout << " [q] Quit" << std::endl;
    std::cout << "Select a live session to join: " << std::endl;
    char selection;
    std::cin >> selection;

    // If the user picked a session, find the root.live folder
    size_t selectionIdx = size_t(selection) - 0x30;
    if (std::isdigit(selection) && selectionIdx < sessionList.size())
    {
        std::string sessionName = sessionList[selectionIdx];
        liveSessionInfo.SetSessionName(sessionName.c_str());

        // Check that the config file version matches
        LiveSessionConfigFile sessionConfig;
        std::string tomlUrl = liveSessionInfo.GetLiveSessionTomlUrl();
        if (!sessionConfig.IsVersionCompatible(tomlUrl.c_str()))
        {
            std::string actualVersion = sessionConfig.GetSessionConfigValue(tomlUrl.c_str(), LiveSessionConfigFile::Key::Version);
            std::cout << "The session config TOML file version is not compatible, exiting." << std::endl;
            std::cout << "Expected: " << LiveSessionConfigFile::kCurrentVersion << " Actual: " << actualVersion << std::endl;
            exit(1);
        }

        std::string liveSessionUrl = liveSessionInfo.GetLiveSessionUrl();
        liveStage = UsdStage::Open(liveSessionUrl);
    }
    else if ('n' == selection)
    {
        // Get a new session name
        bool validSessionName = false;
        std::string sessionName;
        while (!validSessionName)
        {
        std::cout << "Enter the new session name: ";
        std::cin >> sessionName;

            // Session names must start with an alphabetical character, but may contain alphanumeric, hyphen, or underscore characters.
            if (liveSessionInfo.SetSessionName(sessionName.c_str()))
            {
                validSessionName = true;
            }
            else
            {
                std::cout << "Session names must start with an alphabetical character, but may contain alphanumeric, hyphen, or underscore characters." << std::endl;
            }
        }

        // Make sure that this session doesn't already exist (don't overwrite/stomp it)
        if (liveSessionInfo.DoesSessionExist())
        {
            std::cout << "Session config file already exists: " << liveSessionInfo.GetLiveSessionTomlUrl() << std::endl;
            exit(1);
        }

        // Create the session config file 
        LiveSessionConfigFile::KeyMap keyMap;
        std::string stageUrl = liveSessionInfo.GetStageUrl();
        std::string connectedUserName = getConnectedUsername(stageUrl.c_str());
        keyMap[LiveSessionConfigFile::Key::Admin] = connectedUserName.c_str();
        keyMap[LiveSessionConfigFile::Key::StageUrl] = stageUrl.c_str();
        keyMap[LiveSessionConfigFile::Key::Mode] = "default";
        LiveSessionConfigFile sessionConfig;
        if (!sessionConfig.CreateSessionConfigFile(liveSessionInfo.GetLiveSessionTomlUrl().c_str(), keyMap))
        {
            std::cout << "Unable to create session config file: " << liveSessionInfo.GetLiveSessionTomlUrl() << std::endl;
            exit(1);
        }

        // Create the new root.live file to be the stage's edit target
        std::string liveSessionUrl = liveSessionInfo.GetLiveSessionUrl();
        liveStage = UsdStage::CreateNew(liveSessionUrl);
    }
    else
    {
        std::cout << "Invalid selection, exiting";
        exit(1);
    }


    // Get the live layer from the live stage
    SdfLayerHandle liveLayer = liveStage->GetRootLayer();

    // Construct the layers so that we can join the session
    rootStage->GetSessionLayer()->InsertSubLayerPath(liveLayer->GetIdentifier());
    rootStage->SetEditTarget(UsdEditTarget(liveLayer));
}

bool endAndMergeSession(UsdStageRefPtr rootStage, OmniChannel& channel, LiveSessionInfo& liveSessionInfo)
{
    // Do we have authority (check TOML)?
    // Get the current owner name from the session TOML
    LiveSessionConfigFile sessionConfig;
    std::string sessionAdmin = sessionConfig.GetSessionAdmin(liveSessionInfo.GetLiveSessionTomlUrl().c_str());
    std::string currentUser = getConnectedUsername(liveSessionInfo.GetStageUrl().c_str());
    if (sessionAdmin != currentUser)
    {
        std::cout << "You [" << currentUser << "] are not the session admin [" << sessionAdmin << "].  Stopping merge.";
        return false;
    }

    // Gather the latest changes from the live stage
    omniClientLiveProcess();

    // Send a MERGE_STARTED channel message
    channel.SendChannelMessage(OmniChannelMessage::MessageType::MergeStarted);

    // Create a checkpoint on the live layer (don't force if no changes)
    // Create a checkpoint on the root layer (don't force if no changes)
    std::string comment("Pre-merge for " + liveSessionInfo.GetSessionName() + " session");
    checkpointFile(liveSessionInfo.GetLiveSessionUrl(), comment.c_str(), false);
    checkpointFile(liveSessionInfo.GetStageUrl(), comment.c_str(), false);

    // Check if there are any prims defined in the stage's root layer.
    // Merging to a new layer if this is the case could result in no visible changes due to the
    // root layer taking priority
    bool rootHasSpecs = primUtils::RootLayerHasPrimSpecsDefined(rootStage);

    std::string mergeOption("_");
    while (mergeOption != "n" && mergeOption != "r" && mergeOption != "c")
    {
        if (rootHasSpecs)
        {
            std::cout << "\nWARNING: There are PrimSpecs defined in the root layer.  Changes from the live session could be hidden if they are merged to a new layer." << std::endl;
        }
        std::cout << "Merge to new layer [n], root layer [r], or cancel [c]: ";
        std::cin >> mergeOption;
    }
    if (mergeOption == "n")
    {
        // Inject a new layer in the same folder as the root with the session name into the root stage (rootStageName_sessionName_edits.usd)
        std::string stageName = liveSessionInfo.GetStageFileName();
        std::string stageFolder = liveSessionInfo.GetStageFolderUrl();
        std::string sessionName = liveSessionInfo.GetSessionName();
        std::string newLayerUrl = stageFolder + "/" + stageName + "_" + sessionName + ".usd";
        std::cout << "Merging session changes to " << newLayerUrl << " and inserting as a sublayer in the root layer."<< std::endl;
        primUtils::MergeLiveLayerToNewLayer(rootStage->GetEditTarget().GetLayer(), rootStage->GetRootLayer(), newLayerUrl.c_str());
    }
    else if (mergeOption == "r")
    {
        // Merge the live deltas to the root layer
        // This does not clear the source layer --- we'll do that after checkpointing it
        primUtils::MergeLiveLayerToRoot(rootStage->GetEditTarget().GetLayer(), rootStage->GetRootLayer());
    }
    else // the merge was canceled
    {
        return false;
    }

    // Create a checkpoint on the root layer while saving it
    std::string postComment("Post-merge for " + liveSessionInfo.GetSessionName() + " session");
    omniUsdResolverSetCheckpointMessage(postComment.c_str());
    rootStage->GetRootLayer()->Save();
    omniUsdResolverSetCheckpointMessage("");

    // Clear the live layer and process the live changes
    rootStage->GetEditTarget().GetLayer()->Clear();
    omniClientLiveProcess();

    // Remove the .live layer from the session layer
    rootStage->GetSessionLayer()->GetSubLayerPaths().clear();
    rootStage->SetEditTarget(UsdEditTarget(rootStage->GetRootLayer()));

    // Send a MERGE_FINISHED channel message
    channel.SendChannelMessage(OmniChannelMessage::MessageType::MergeFinished);

    return true;
}


// Perform a live edit on the box
static void liveEdit(UsdGeomMesh &meshIn, OmniChannel& channel)
{
    double angle = 0;

    static const std::string promptMsg = "\nEnter an option:\n" \
        " [t] transform the mesh\n" \
        " [r] rename a prim\n" \
        " [o] list session owner/admin\n" \
        " [u] list session users\n" \
        " [g] emit a GetUsers message (note there will be no response unless another app is connected to the same session)\n" \
        " [c] log contents of the session config file\n" \
        " [m] merge changes and end the session\n" \
        " [q] quit."
    ;

    // Process any updates that may have happened to the stage from another client
    omniClientLiveProcess();
    {
        std::unique_lock<std::mutex> lk(gLogMutex);
        std::cout << "Begin Live Edit on " << meshIn.GetPath() << " - " << std::endl;
    }

    bool wait = true;
    while (wait)
    {
#ifdef _WIN32
        char nextCommand = _getch();
#else
        char nextCommand = getchar();
#endif
        // Check if the live session was merged by another client and exit if so
        // A more sophisticated client should reload the stage without the live session layer
        if (gStageMerged)
        {
            return;
        }

        // Process any updates that may have happened to the stage from another client
        omniClientLiveProcess();

        switch (nextCommand) {
        case 't':
        {
            if (!meshIn)
                break;
                
            // Increase the angle
            angle += 15;
            if (angle >= 360)
                angle = 0;

            double radians = angle * 3.1415926 / 180.0;
            double x = sin(radians) * 10;
            double y = cos(radians) * 10;

            GfVec3d position(0);
            GfVec3d rotXYZ(0);
            GfVec3d scale(1);
            if( !xformUtils::getLocalTransformSRT( meshIn.GetPrim(), position, rotXYZ, gDefaultRotationOrder, scale ) )
            {
                std::cerr << "WARNING: Unable to read transformation on \"" << meshIn.GetPath() << "\"" << std::endl;
                continue;
            }

            // Move/Rotate the existing position/rotation - this works for Y-up stages
            position += GfVec3d(x, 0, y);
            rotXYZ = GfVec3d(rotXYZ[0], angle, rotXYZ[2]);

            xformUtils::setLocalTransformSRT( meshIn.GetPrim(), position, rotXYZ, gDefaultRotationOrder, scale );
            {
                std::unique_lock<std::mutex> lk(gLogMutex);
                std::cout << "Setting pos: " << position << " and rot: " << rotXYZ << std::endl;
            }

            // Commit the change to USD
            omniClientLiveProcess();
            break;
        }
        case 'r':
        {
            std::string primToRename;
            UsdPrim prim;
            std::cout << "Enter complete prim path to rename: ";
            std::cin >> primToRename;

            // Traverse the stage and return the first UsdGeomMesh we find
            auto range = gStage->Traverse();
            for (const auto& node : range)
            {
                //std::cout << "Node: " << node.GetPath() << std::endl;
                if (node.GetPath().GetString() == primToRename)
                {
                    prim = node;
                }
            }
            if (!prim)
            {
                std::cout << "Could not find prim: " << primToRename << std::endl;
            }
            else
            {
                std::string newName;
                std::cout << "Enter new prim name (not entire path): ";
                std::cin >> newName;

                std::string validNewName = TfMakeValidIdentifier(newName);
                if (validNewName != newName)
                {
                    std::cout << "\"" << newName << "\" is not valid, renaming to \"" << validNewName << "\"" << std::endl;
                    newName = validNewName;
                }

                if (primUtils::RenamePrim(prim, TfToken(newName)))
                {
                    // Commit the change to USD
                    omniClientLiveProcess();
                    std::cout << primToRename << " renamed to: " << newName << std::endl;
                }
                else
                {
                    std::cout << primToRename << " rename failed." << std::endl;
                }
            }
            break;
        }
        case 'o':
        {
            LiveSessionConfigFile sessionConfig;
            std::string sessionAdmin = sessionConfig.GetSessionConfigValue(gLiveSessionInfo.GetLiveSessionTomlUrl().c_str(), LiveSessionConfigFile::Key::Admin);
            std::cout << "Session Admin: " << sessionAdmin << std::endl;
            break;
        }
        case 'u':
        {
            size_t userCount = channel.GetUsersCount();
            std::cout << "Listing Session users:" << std::endl;
            if(!userCount)
            {
                std::cout << " - No other users in session" << std::endl;
            }
            for (size_t i = 0; i < userCount; i++)
            {
                const OmniPeerUser& user = channel.GetUserAtIndex(i);
                std::cout << " - " << user.userName << "[" << user.app << "]" << std::endl;
            }
            break;
        }
        case 'g':
        {
            // Send a GET_USERS channel message, all the other connected clients will respond with a "HELLO"
            // The callbacks could be used to fill in a UI list of connected users when browsing sessions.
            // This is done _BEFORE_ joining a session, but it's convenient to just put it here as an example
            std::cout << "Blasting GET_USERS message to channel" << std::endl;
            channel.SendChannelMessage(OmniChannelMessage::MessageType::GetUsers);
            break;
        }
        case 'c':
        {
            std::cout << "Retrieving session config file: " << std::endl;
            LiveSessionConfigFile sessionConfig;
            for( auto k : {
                LiveSessionConfigFile::Key::Version,
                LiveSessionConfigFile::Key::Admin,
                LiveSessionConfigFile::Key::StageUrl,
                LiveSessionConfigFile::Key::Description,
                LiveSessionConfigFile::Key::Mode,
                LiveSessionConfigFile::Key::Name
            })
            {
                std::string value = sessionConfig.GetSessionConfigValue(gLiveSessionInfo.GetLiveSessionTomlUrl().c_str(), k);
                if(!value.empty())
                {
                    std::cout << " " << sessionConfig.KeyToString(k) << " = \"" << value << "\"" << std::endl;
                }
            }
            break;
        }
        case 'm':
        {
            std::cout << "Merging session changes to root layer, Live Session complete\n";
            if (endAndMergeSession(gStage, channel, gLiveSessionInfo))
            {
                wait = false;
            }
            break;
        }
        //escape or 'q'
        case 27:
        case 'q':
        {
            wait = false;
            std::cout << "Live Edit complete\n";
            break;
        }
        default:
            std::cout << promptMsg << std::endl;
        }
    }
}

// Returns true if the provided maybeURL contains a host and path
static bool isValidOmniURL(const std::string& maybeURL)
{
    bool isValidURL = false;
    OmniClientUrl* url = omniClientBreakUrl(maybeURL.c_str());
    if (url->host && url->path && 
        (std::string(url->scheme) == std::string("omniverse") ||
         std::string(url->scheme) == std::string("omni")))
    {
        isValidURL = true;
    }
    omniClientFreeUrl(url);
    return isValidURL;
}

// Print the command line arguments help
static void printCmdLineArgHelp()
{
    std::cout << "Usage: samples [options]" << std::endl;
    std::cout << "  options:" << std::endl;
    std::cout << "    -h, --help                    Print this help" << std::endl;
    std::cout << "    -e, --existing path_to_stage  Open an existing stage and perform live transform edits (full omniverse URL)" << std::endl;
    std::cout << "    -v, --verbose                 Show the verbose Omniverse logging" << std::endl;
    std::cout << "\n\nExamples:\n";
    std::cout << "\n * live edit a stage on the ov-prod server at /Projects/LiveEdit/livestage.usd" << std::endl;
    std::cout << "    > samples -e omniverse://ov-prod/Projects/LiveEdit/livestage.usd" << std::endl;
}

// This class contains a run() method that's run in a thread to
// tick any message channels (it will flush any messages received
// from the Omniverse Client Library
class AppUpdate
{
public:
    AppUpdate(int updatePeriodMs)
        : mUpdatePeriodMs(updatePeriodMs)
        , mStopped(false)
    {}

    void run()
    {
        while (!mStopped)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(mUpdatePeriodMs));
            for (OmniChannel* channel : mChannels)
            {
                channel->Update();
            }
        }
    }

    int mUpdatePeriodMs;
    bool mStopped;
    std::vector<OmniChannel*> mChannels;
};

// A callback for any message that comes from the session channel
void channelMessageCb(OmniChannelMessage::MessageType messageType, void* userData, const char* userName, const char* appName)
{
    // Just a note that userData is available for context
    std::string* existingStage = static_cast<std::string*>(userData);

    std::cout << "Channel Callback: " << OmniChannelMessage::MessageTypeToStringType(messageType) << " " << userName << " - " << appName << std::endl;
    if (messageType == OmniChannelMessage::MessageType::MergeStarted ||
        messageType == OmniChannelMessage::MessageType::MergeFinished)
    {
        std::cout << "Exiting since a merge is happening in another client" << std::endl;
        gStageMerged = true;
    }
}

// Main Application 
int main(int argc, char*argv[])
{
    bool doLiveEdit = false;
    std::string existingStage;
    UsdGeomMesh boxMesh;

    // Process the arguments, if any
    for (int x = 1; x < argc; x++)
    {
        if (strcmp(argv[x], "-h") == 0 || strcmp(argv[x], "--help") == 0)
        {
            printCmdLineArgHelp();
            return 0;
        }
        else if (strcmp(argv[x], "-v") == 0 || strcmp(argv[x], "--verbose") == 0)
        {
            gOmniverseLoggingEnabled = true;
        }
        else if (strcmp(argv[x], "-e") == 0 || strcmp(argv[x], "--existing") == 0)
        {
            doLiveEdit = true;
            if (x == argc-1)
            {
                std::cout << "ERROR: Missing an Omniverse URL to the stage to edit.\n" << std::endl;
                printCmdLineArgHelp();
                return -1;
            }
            existingStage = std::string(argv[++x]);
            if (!isValidOmniURL(existingStage))
            {
                std::cout << "This is not an Omniverse Nucleus URL: " << existingStage << std::endl;
                return -1;
            }
        }
        else
        {
            std::cout << "Unrecognized option: " << argv[x] << std::endl;
        }
    }

    if (existingStage.length() == 0)
    {
        std::cout << "An existing stage must be supplied with the -e argument: " << std::endl;
        return -1;
    }

    // Startup Omniverse with the default login
    if (!startOmniverse())
        exit(1);

    // Find a UsdGeomMesh in the existing stage
    // This will initialize gStage
    boxMesh = findGeomMesh(existingStage);

    gLiveSessionInfo.Initialize(existingStage.c_str());

    findOrCreateSession(gStage, gLiveSessionInfo);

    gOmniChannel.SetChannelUrl(gLiveSessionInfo.GetMessageChannelUrl().c_str());
    gOmniChannel.SetAppName(gAppName);
    gOmniChannel.RegisterNotifyCallback(channelMessageCb, &existingStage);
    gOmniChannel.JoinChannel();

    // Create a thread that "ticks" every 16ms
    // The only thing it does is "Update" the Omniverse Message Channels to
    // flush out any messages in the queue that were received.
    AppUpdate appUpdate(16);
    appUpdate.mChannels.push_back(&gOmniChannel);
    std::thread channelUpdateThread = std::thread(&AppUpdate::run, &appUpdate);

    // Do a live edit session moving the box around, changing a material
    if (doLiveEdit )
    {
        liveEdit(boxMesh, gOmniChannel);
    }
    gOmniChannel.LeaveChannel();
    appUpdate.mStopped = true;
    channelUpdateThread.join();

    // All done, shut down our connection to Omniverse
    shutdownOmniverse();

    return 0;
}
