#ifndef CONSTANT_SET_GENERATOR_H_
#define CONSTANT_SET_GENERATOR_H_

#include <string>
#include <vector>

#include "ast_visitor.h"

class ConstantSetGenerator : public AstVisitor {
private:
  // Store the constants appearing in original program
  std::set<int> constant_set;

public:
  std::string
  ast_visit_transform(const clang::TranslationUnitDecl *tu_decl) override;
  std::set<int> get_set();
  void insert_constant(int val);

protected:
  std::string ast_visit_integer_literal(
      const clang::IntegerLiteral *integer_literal) override;
};

#endif // CONSTANT_SET_GENERATOR_H_
