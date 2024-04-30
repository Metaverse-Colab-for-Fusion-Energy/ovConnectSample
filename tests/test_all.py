# This is expected to be run from the root folder of the samples repo

import argparse
import logging
import os
import platform
import subprocess
import tempfile
import time

import log

LOGGER = log.get_logger("TestAllSamples", level=logging.INFO)
g_base_url_env_key = "OMNI_BASE_URL"
g_default_base_url = "omniverse://localhost/Projects/samplesTest"

def shell_ext():
    if platform.system() == "Windows":
        return ".bat"
    else:
        return ".sh"

def run_omnicli(*argv):
    cmdline = list()
    cmdline.append(f"omnicli{shell_ext()}")
    cmdline += argv
    
    LOGGER.info("Running: " + str(cmdline))
    
    completed = subprocess.run(cmdline, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    LOGGER.info(completed.stdout.decode('utf-8'))
    return completed.returncode


def run_shell_script(script, *argv):
    cmdline = list()
    cmdline.append(os.path.join(os.getcwd(),script + shell_ext()))
    cmdline += argv
    
    LOGGER.info("Running: " + str(cmdline))
    completed = subprocess.run(cmdline, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    LOGGER.info(completed.stdout.decode('utf-8'))
    return completed.returncode, completed.stdout.decode('utf-8')

# For now, just expect these to be run in alphabetical order.  Address this if not the case.
def clean_base_url():
    base_url = os.getenv(g_base_url_env_key, g_default_base_url)
    return_code, output = run_shell_script("omnicli", "delete", base_url)
    # the folder may not be present, so handle stdout to see if that's the case
    if "Error: Not Found" in output:
        return
    assert(return_code == 0)

def test_helloworld_cpp():
    #clean_base_url()
    base_url = os.getenv(g_base_url_env_key, g_default_base_url)
    stage_url = base_url + "/" + "helloworld.usd"
    return_code, output = run_shell_script("run_hello_world", "-p", base_url)
    assert(return_code == 0)
    return_code, output = run_shell_script("omnicli", "stat", stage_url)
    assert(return_code == 0)
    return_code, output = run_shell_script("omni_asset_validator", stage_url)
    assert(return_code == 0)
    for line in output.splitlines():
        if "Error" in line:
            if "OmniPBR.mdl" in line:
                pass
            else:
                assert(0)

def test_helloworld_py():
    #clean_base_url()
    base_url = os.getenv(g_base_url_env_key, g_default_base_url)
    stage_url = base_url + "/" + "helloworld_py.usd"
    return_code, output = run_shell_script("run_py_hello_world", "-p", base_url)
    assert(return_code == 0)
    return_code, output = run_shell_script("omnicli", "stat", stage_url)
    assert(return_code == 0)
    return_code, output = run_shell_script("omni_asset_validator", stage_url)
    assert(return_code == 0)
    for line in output.splitlines():
        if "Error" in line:
            if "OmniPBR.mdl" in line:
                pass
            else:
                assert(0)

def test_helloworld_py_extents_failure():
    #clean_base_url()
    base_url = os.getenv(g_base_url_env_key, g_default_base_url)
    stage_url = base_url + "/" + "helloworld_py.usd"
    return_code, output = run_shell_script("omnicli", "stat", stage_url)
    if return_code != 0:
        return_code, output = run_shell_script("run_py_hello_world", "-p", base_url)
        assert(return_code == 0)
    return_code, output = run_shell_script("run_py_hello_world", "-e", stage_url, "-f")
    assert(return_code == 0)
    return_code, output = run_shell_script("omni_asset_validator", stage_url)
    assert("ExtentsChecker" in output)

# This test could be watching the live data, currently it's just checking that it exists
def test_simple_sensor():
    base_url = os.getenv(g_base_url_env_key, g_default_base_url)
    stage_url = base_url + "/" + "SimpleSensorExample.live"
    box_process_count = "8"
    process_lifetime_seconds = "10"
    return_code, output = run_shell_script("run_omniSimpleSensor", base_url, box_process_count, process_lifetime_seconds)
    assert(return_code == 0)
    #This sleep not necessary, it looks like we're already waiting    
    #LOGGER.info(f"Sleeping for {process_lifetime_seconds} while SimpleSensor runs...")
    #time.sleep(float(process_lifetime_seconds))
    return_code, output = run_shell_script("omnicli", "stat", stage_url)
    assert(return_code == 0)
    return_code, output = run_shell_script("omni_asset_validator", stage_url)
    assert(return_code == 0)
    for line in output.splitlines():
        if "Error" in line:
            assert(0)

# This test exercises some copy and move functionality with omnicli (since adding overwrite by default)
# NOTE: this test can't use textures since interaction with the Nucleus Thumbnail Service can cause issues (OM-80653)
def test_omnicli_copy_and_move():
    base_url = os.getenv(g_base_url_env_key, g_default_base_url)
    local_folder = "deps"
    nucleus_folder = base_url + "/CopyTest"
    nucleus_copy_dst_folder = base_url + "/CopyTestDup"
    nucleus_move_dst_folder = base_url + "/MoveTest"

    # Delete the folder
    return_code, output = run_shell_script("omnicli", "delete", nucleus_folder)
    return_code, output = run_shell_script("omnicli", "delete", nucleus_copy_dst_folder)
    return_code, output = run_shell_script("omnicli", "delete", nucleus_move_dst_folder)

    # Validate list
    return_code, output = run_shell_script("omnicli", "list", local_folder)
    assert(return_code == 0)

    # Copy a folder to Nucleus
    return_code, output = run_shell_script("omnicli", "copy", local_folder, nucleus_folder)
    assert(return_code == 0)

    # Validate list
    return_code, output = run_shell_script("omnicli", "list", nucleus_folder)
    assert(return_code == 0)

    # Copy a folder from Nucleus to Nucleus
    return_code, output = run_shell_script("omnicli", "copy", nucleus_folder, nucleus_copy_dst_folder)
    assert(return_code == 0)

    # Validate list
    return_code, output = run_shell_script("omnicli", "list", nucleus_copy_dst_folder)
    assert(return_code == 0)

    # Copy a folder from Nucleus to Nucleus (overwrite)
    return_code, output = run_shell_script("omnicli", "copy", nucleus_folder, nucleus_copy_dst_folder)
    assert(return_code == 0)

    # Move a folder from Nucleus to Nucleus
    return_code, output = run_shell_script("omnicli", "move", nucleus_copy_dst_folder, nucleus_move_dst_folder)
    assert(return_code == 0)

    # Validate list (move src is gone)
    return_code, output = run_shell_script("omnicli", "list", nucleus_copy_dst_folder)
    assert(return_code != 0)

    # Validate list (move dst exists)
    return_code, output = run_shell_script("omnicli", "list", nucleus_move_dst_folder)
    assert(return_code == 0)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Test for all Connect Samples",
                                     formatter_class=argparse.ArgumentDefaultsHelpFormatter)

    parser.add_argument("-p", "--path", action="store", default=g_default_base_url)
    args = parser.parse_args()
    g_default_base_url = args.path

    test_helloworld_cpp()
    test_helloworld_py()
    test_helloworld_py_extents_failure()
    test_simple_sensor()
    test_omnicli_copy_and_move()
