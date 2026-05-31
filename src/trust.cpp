// src/trust.cpp — точка входа в компилятор/транспилятор Trust
#include "pipeline/pipeline.hpp"

int main(int argc, char* argv[], char* envp[]) {
    return trust::Pipeline::main(argc, argv, envp);
}