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
  std::cerr << "Usage: constant_set <source_file> <bit_size> content/size"
            << std::endl;
}

int main(int argc, const char **argv) {
  try {
    // Block out SIGINT, because we can't handle it properly
    signal(SIGINT, SIG_IGN);

    if (argc == 4) {
      const auto string_to_parse = file_to_str(std::string(argv[1]));
      int bit_size = std::stoi(std::string(argv[2]));
      // if output == content, output the content of constant_set
      // if output == size, output the log(2,size) of the constant_set
      std::string output = std::string(argv[3]);
      assert(output == "content" or output == "size");

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
      if (output == "content") {
        // Output should be like the format {0,1,2,3}
        std::string constant_group_vector = "{";
        for (auto i : constant_set) {
          if (constant_group_vector.length() != 1) {
            constant_group_vector += ", ";
          }
          constant_group_vector += std::to_string(i);
        }
        constant_group_vector += "}";
        std::cout << constant_group_vector << std::endl;
      } else {
        assert(output == "size");
        std::cout << ceil(log2(constant_set.size())) << std::endl;
      }

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
