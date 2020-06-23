#include <csignal>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <stdlib.h>
#include <string>
#include <utility>

#include "third_party/assert_exception.h"

#include "chipmunk_another_domino_generator.h"
#include "chipmunk_deadcode_generator.h"
#include "compiler_pass.h"
#include "pkt_func_transform.h"
#include "util.h"

// For the _1, and _2 in std::bind
// (Partial Function Application)
using std::placeholders::_1;

void print_usage() {
  std::cerr << "Usage: mutator <source_file>" << std::endl;
}

int main(int argc, const char **argv) {
  try {
    // Block out SIGINT, because we can't handle it properly
    signal(SIGINT, SIG_IGN);

    if (argc == 2) {
      int num_of_transformed_file = 1;
      std::string domino_file_name = std::string(argv[1]);
      std::size_t pos_begin = domino_file_name.rfind("/");
      // avoid the case where the file name may be like ../domino-example/rcp.c
      std::size_t pos_end = domino_file_name.rfind(".c");

      domino_file_name =
          domino_file_name.substr(pos_begin + 1, pos_end - pos_begin - 1);
     
      // Fix the rand_seed value
      std::srand(1);

      while (num_of_transformed_file != 11) {
        const auto string_to_parse = file_to_str(std::string(argv[1]));
        ChipmunkAnotherDominoGenerator AnotherDomino;
        auto chipmunk_another_domino_generator = SinglePass<>(
            std::bind(&ChipmunkAnotherDominoGenerator::ast_visit_transform,
                      AnotherDomino, _1));

        int mutation_num = 0;
        std::string sketch_program =
            chipmunk_another_domino_generator(string_to_parse);

        while (mutation_num != 10) {
          int random_num = rand() % 8 + 1;
          // Now just avoid the change to add (3*4-12)*10 and avoid 
          // add too many temporary vars 

          // TODO: try to expand to other possible mutator generation ways
          if (random_num != 2 && random_num != 3 && random_num != 7)
            continue;
          mutation_num++;
          // random_num is to record which execution to take
          if (random_num >= 1 && random_num <= 3) {
            ChipmunkAnotherDominoGenerator another_domino;
            another_domino.round = mutation_num;
            another_domino.rand = random_num;
            auto chipmunk_another_domino_generator = SinglePass<>(
                std::bind(&ChipmunkAnotherDominoGenerator::ast_visit_transform,
                          another_domino, _1));
            sketch_program = chipmunk_another_domino_generator(sketch_program);
          } else {
            ChipmunkDeadcodeGenerator domino_with_deadcode;
            domino_with_deadcode.rand = random_num;
            auto chipmunk_deadcode_generator = SinglePass<>(
                std::bind(&ChipmunkDeadcodeGenerator::ast_visit_transform,
                          domino_with_deadcode, _1));
            sketch_program = chipmunk_deadcode_generator(sketch_program);
          }
        }
        std::string filename = "/tmp/" + domino_file_name + "_equivalent_" +
                               std::to_string(num_of_transformed_file) + ".c";
        std::ofstream myfile;
        myfile.open(filename.c_str());
        myfile << sketch_program;
        myfile.close();
        num_of_transformed_file++;
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
