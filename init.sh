conda activate py312
source check_and_set_env.sh
git submodule update --init --recursive

if [  -n "${ZSH_VERSION:-}" ]; then
    DIR="$(readlink -f -- "${(%):-%x}")"
    SDK_HOME=$(dirname $DIR)
else
    SDK_HOME="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
fi

if [ -n "${GVSOC_WORKDIR}" ]; then
    SDK_INSTALL=${GVSOC_WORKDIR}
else
    SDK_INSTALL=${SDK_HOME}
fi

export PATH=$SDK_INSTALL/install/bin:$PATH
export PYTHONPATH=$SDK_INSTALL/install/python:$PYTHONPATH


if [ ! -d "third_party" ]; then
    echo "Running toolchain preparation..."
    make third_party/toolchain
fi

export PATH=$SDK_HOME/third_party/toolchain/install/bin:$PATH
