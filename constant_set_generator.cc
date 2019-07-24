#include "constant_set_generator.h"

#include <iostream>
#include <string>

#include "third_party/assert_exception.h"

#include "clang_utility_functions.h"

using namespace clang;

std::string ConstantSetGenerator::ast_visit_transform(
    const clang::TranslationUnitDecl *tu_decl) {
  // TODO: Need to check if we have more than one packet func per tu_decl and
  // report an error if so.
  for (const auto *decl : dyn_cast<DeclContext>(tu_decl)->decls()) {
    if (isa<FunctionDecl>(decl) and
        (is_packet_func(dyn_cast<FunctionDecl>(decl)))) {
      // record body part first
      std::string body_part =
          ast_visit_stmt(dyn_cast<FunctionDecl>(decl)->getBody());
      return "";
    }
  }
  assert_exception(false);
}

void ConstantSetGenerator::insert_constant(int val) {
  constant_set.insert(val);
}

std::set<int> ConstantSetGenerator::get_set() { return constant_set; }

std::string ConstantSetGenerator::ast_visit_integer_literal(
    const clang::IntegerLiteral *integer_literal) {
  assert_exception(integer_literal);
  int const_val = stoi(clang_stmt_printer(integer_literal));
  insert_constant(const_val);
  return clang_stmt_printer(integer_literal);
}
