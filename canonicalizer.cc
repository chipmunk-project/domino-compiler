#include <csignal>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <utility>

#include "compiler_pass.h"
#include "pkt_func_transform.h"
#include "rename_domino_code_generator.h"
#include "third_party/assert_exception.h"
#include "util.h"

// For the _1, and _2 in std::bind
// (Partial Function Application)
using std::placeholders::_1;

void print_usage() {
  std::cerr << "Usage: canonicalizer <source_file>" << std::endl;
}

std::string get_filename(std::string s) {
  std::size_t found_dot = s.rfind(".c");
  std::size_t found_slash = s.rfind('/');
  std::size_t start_pos = 0;
  std::string filename;

  if (found_slash!=std::string::npos) {
    start_pos = found_slash + 1;
  }
  filename = s.substr(start_pos, found_dot - start_pos);
  return filename;
}

int main(int argc, const char **argv) {
  try {
    // Block out SIGINT, because we can't handle it properly
    signal(SIGINT, SIG_IGN);

    if (argc == 2) {
      std::string file_name = get_filename(std::string(argv[1]));
      const auto string_to_parse = file_to_str(std::string(argv[1]));

      auto rename_domino_code_generator = SinglePass<>(
          std::bind(&RenameDominoCodeGenerator::ast_visit_transform,
                    RenameDominoCodeGenerator(), _1));

      std::string sketch_program = "/* \n// Original program: \n" + string_to_parse + " */\n";

      sketch_program += rename_domino_code_generator(string_to_parse);

      std::string output_filename = "/tmp/" + file_name + "_canonicalizer.c";
      std::ofstream myfile;
      myfile.open(output_filename.c_str());
      myfile << sketch_program;
      myfile.close();

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
