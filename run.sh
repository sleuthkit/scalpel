#!/usr/bin/env bash


# set -x
ERRORS=()

PWD=$(pwd)

if [[ ! -f ${PWD}/device.img ]]; then
  ERRORS+=("No ${PWD}/device.img file available!")
fi

# if [[ ! -d ${PWD}/recovery ]]; then
#   ERRORS+=("No ${PWD}/recovery directory available!")
# fi

function print_errors() {
  # echo Num of array items "${#ERRORS[@]}"
  if [[ ${#ERRORS[*]} -gt 0 ]]; then
    echo "There are ${#ERRORS[@]} errors:"
    for item in "${ERRORS[@]}"; do
      echo "- $item"
    done
    return 1
  fi
  return 0
}

print_errors || exit 1

docker run --rm -it \
  -v ${PWD}/device.img:/scalpel/device.img \
  -v ${PWD}/recovery:/scalpel/recovery \
s4ros/scalpel $@
