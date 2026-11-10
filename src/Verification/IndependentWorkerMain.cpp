#include "IndependentVerifier.hpp"
#include "WorkerProtocol.hpp"

int main(int argc, char* argv[]) {
    return opusoptima::verification::worker::RunWorker(
        argc, argv, opusoptima::verification::VerifyIndependentFiles);
}
