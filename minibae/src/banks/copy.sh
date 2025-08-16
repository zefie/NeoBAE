#!/bin/bash
BANK_DIR="$(realpath "$(dirname "${0}")")"
TARGET_DIR="${1}"
#84bc3eda6f973e52afb3f203c6c8a7078a35c1d3  src/banks/BankDevelopment/Final/npatches.hsb
#df1225ef07b615638513f88801aedf8a6a8767b3  src/banks/BankDevelopment/Final/nupatches.hsb
cp "${BANK_DIR}/BankDevelopment/HruskaNokiaSoundsetRev1/NPatches.hsb" "${TARGET_DIR}/minibae-hruskanokia-rev1.hsb"
cp "${BANK_DIR}/BankDevelopment/SalterNokiaRev3/NokiaRev BS3.hsb" "${TARGET_DIR}/NokiaRev BS3.hsb"
cp "${BANK_DIR}/BankDevelopment/SalterNokiaRev4/NokiaRev BS4.hsb" "${TARGET_DIR}/NokiaRev BS4.hsb"
cp "${BANK_DIR}/BankDevelopment/HruskaNokiaSoundsetRev5/HruskaNokiaRev5Soundset.hsb" "${TARGET_DIR}/minibae-hruskanokia-rev5.hsb"
cp "${BANK_DIR}/BankDevelopment/SalterNokiaRev8a/nupatches.hsb" "${TARGET_DIR}/minibae-salternokia-rev8a.hsb"
cp "${BANK_DIR}/patches/patches.hsb" "${TARGET_DIR}/soundbank-min.gm"
cp "${BANK_DIR}/patches111/patches111.hsb" "${TARGET_DIR}/patches111-uncompressed.hsb"
cp "${BANK_DIR}/patchesp/patchesp.hsb" "${TARGET_DIR}/patchesp.hsb"
cp "${BANK_DIR}/SonyUIQBank/SonyUIQBank_P800.hsb" "${TARGET_DIR}/sony-ericsson-p800.hsb"
cp "${BANK_DIR}/SonyUIQBank/SonyUIQBank_P900.hsb" "${TARGET_DIR}/sony-ericsson-p900.hsb"
cp "${BANK_DIR}/WTVBanks/wpatches-classic.hsb" "${TARGET_DIR}/minibae-wtv.hsb"
cp "${BANK_DIR}/WTVBanks/wpatches-plus.hsb" "${TARGET_DIR}/patches-wtv.hsb"
find "${BANK_DIR}/other/" -type f -exec cp {} "${TARGET_DIR}/" \;
#8664e092ecfd66e0adbbee871c9ff5bbbbaf0494  src/banks/nChippyBank/nChippyBank.hsb
