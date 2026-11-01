// Optional host diagnostic: g++ with the local TFLM and FlatBuffers headers.
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include "tensorflow/lite/schema/schema_generated.h"
int main(int argc, char **argv) {
    if (argc != 2) return 1;
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<char> data((std::istreambuf_iterator<char>(file)), {});
    flatbuffers::Verifier verifier(reinterpret_cast<const uint8_t *>(data.data()), data.size());
    if (!tflite::VerifyModelBuffer(verifier)) return 2;
    auto model = tflite::GetModel(data.data());
    for (auto code : *model->operator_codes())
        printf("%s version=%d\n", tflite::EnumNameBuiltinOperator(code->builtin_code()), code->version());
    return 0;
}
