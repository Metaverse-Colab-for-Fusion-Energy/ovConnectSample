# THIS FILE IS AUTOGENERATED BY repo_build
import sys
import os
p = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
if os.path.exists(f"" + os.getenv("PYTHONPATH", default="") + ""):
    sys.path.append(f"" + os.getenv("PYTHONPATH", default="") + "")
if os.path.exists(f"{p}/../../../" + os.getenv("PYTHONPATH", default="") + ""):
    sys.path.append(f"{p}/../../../" + os.getenv("PYTHONPATH", default="") + "")
if os.path.exists(f""):
    sys.path.append(f"")
if os.path.exists(f"{p}/../../.."):
    sys.path.append(f"{p}/../../..")
if os.path.exists(f"{p}/../../pip-packages"):
    sys.path.append(f"{p}/../../pip-packages")
if os.path.exists(f"{p}/../../../_repo/deps/repo_man"):
    sys.path.append(f"{p}/../../../_repo/deps/repo_man")
if os.path.exists(f"{p}/../../../_repo/deps/repo_build"):
    sys.path.append(f"{p}/../../../_repo/deps/repo_build")
if os.path.exists(f"{p}/../../../_repo/deps/repo_fileutils"):
    sys.path.append(f"{p}/../../../_repo/deps/repo_fileutils")
if os.path.exists(f"{p}/../../../_repo/deps/repo_format"):
    sys.path.append(f"{p}/../../../_repo/deps/repo_format")
if os.path.exists(f"{p}/../../../_repo/deps/repo_package"):
    sys.path.append(f"{p}/../../../_repo/deps/repo_package")
if os.path.exists(f"{p}/../../../tools/repoman"):
    sys.path.append(f"{p}/../../../tools/repoman")
if os.path.exists(f"{p}/bindings-python"):
    sys.path.append(f"{p}/bindings-python")
if os.path.exists(f"" + os.getenv("PATH", default="") + ""):
    os.environ["PATH"] += os.pathsep + f"" + os.getenv("PATH", default="") + ""
if os.path.exists(f"{p}/../../../" + os.getenv("PATH", default="") + ""):
    os.environ["PATH"] += os.pathsep + f"{p}/../../../" + os.getenv("PATH", default="") + ""
if os.path.exists(f"{p}/."):
    os.environ["PATH"] += os.pathsep + f"{p}/."
