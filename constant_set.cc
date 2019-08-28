#include <algorithm>
#include <csignal>
#include <functional>
#include <iostream>
#include <math.h>
#include <set>
#include <string>
#include <utility>

#include "third_party/assert_exception.h"

#include "compiler_pass.h"
#include "constant_set_generator.h"
#include "pkt_func_transform.h"
#include "util.h"

// For the _1, and _2 in std::bind
// (Partial Function Application)
using std::placeholders::_1;

void print_usage() {
  std::cerr << "Usage: constant_set <source_file> <bit_size>"
            << std::endl;
}

int main(int argc, const char **argv) {
  try {
    // Block out SIGINT, because we can't handle it properly
    signal(SIGINT, SIG_IGN);

    if (argc == 3) {
      const auto string_to_parse = file_to_str(std::string(argv[1]));
      int bit_size = std::stoi(std::string(argv[2]));

      ConstantSetGenerator constant_set_generator;

      auto generator_single_pass =
          SinglePass<>(std::bind(&ConstantSetGenerator::ast_visit_transform,
                                 &constant_set_generator, _1));
      std::string sketch_program = generator_single_pass(string_to_parse);

      // Use constant_collection to store constants
      std::set<int> constant_set(constant_set_generator.get_set());

      // Add [0, 2^bit_size) into the constant_set
      int power = int(pow(2, bit_size));
      for (int i = 0; i != power; i++) {
        constant_set.insert(i);
      }
      // Output should be like the format 0,1,2,3
      std::string constant_group_vector_str = "";
      for (auto i : constant_set) {
        if (constant_group_vector_str.length() != 0) {
          constant_group_vector_str += ", ";
        }
        constant_group_vector_str += std::to_string(i);
      }
      std::cout << constant_group_vector_str << std::endl;

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
