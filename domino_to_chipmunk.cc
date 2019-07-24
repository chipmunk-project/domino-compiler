#include <csignal>
#include <functional>
#include <algorithm>
#include <math.h> 
#include <iostream>
#include <set>
#include <string>
#include <utility>

#include "third_party/assert_exception.h"

#include "chipmunk_code_generator.h"
#include "compiler_pass.h"
#include "pkt_func_transform.h"
#include "util.h"

// For the _1, and _2 in std::bind
// (Partial Function Application)
using std::placeholders::_1;

void print_usage() {
  std::cerr << "Usage: domino_to_chipmunk <source_file>" << std::endl;
}

int main(int argc, const char **argv) {
  try {
    // Block out SIGINT, because we can't handle it properly
    signal(SIGINT, SIG_IGN);

    if (argc == 2) {
      const auto string_to_parse = file_to_str(std::string(argv[1]));

      ChipmunkCodeGenerator code_generator;

      auto chipmunk_code_generator =
          SinglePass<>(std::bind(&ChipmunkCodeGenerator::ast_visit_transform,
                                 &code_generator, _1));

      std::cout << "/* \n// Original program: \n" + string_to_parse + " */\n"
                << std::endl;

      std::string sketch_program = chipmunk_code_generator(string_to_parse);
      std::cout << sketch_program << std::endl;

      return EXIT_SUCCESS;
    } else {
      print_usage();
      return EXIT_FAILURE;
    }
  } catch (const std::exception &e) {
    std::cerr << "Caught exception in main " << std::endl
              << e.what() << std::endl;
    return EXIT_FAILURE;
  }
}
