#include "rename_domino_code_generator.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <string>

#include "third_party/assert_exception.h"

#include "clang_utility_functions.h"

using namespace clang;

std::string RenameDominoCodeGenerator::ast_visit_transform(
    const clang::TranslationUnitDecl *tu_decl) {
  // TODO: First pass to set up the map
  for (const auto *decl : dyn_cast<DeclContext>(tu_decl)->decls()) {
    if (isa<FunctionDecl>(decl) and
        (is_packet_func(dyn_cast<FunctionDecl>(decl)))) {
      std::string body_part =
          ast_visit_stmt(dyn_cast<FunctionDecl>(decl)->getBody());
    }
  }
//  print_map();

  // TODO: Need to check if we have more than one packet func per tu_decl and
  // report an error if so.
  std::string stateful_var_def = "";
  std::string struct_def = "";
  std::string other_func_def = "";
  for (const auto *decl : dyn_cast<DeclContext>(tu_decl)->decls()) {
    if (isa<FunctionDecl>(decl) and
        (is_packet_func(dyn_cast<FunctionDecl>(decl)))) {
      // record body part first
      std::string body_part =
          ast_visit_stmt(dyn_cast<FunctionDecl>(decl)->getBody());
      return struct_def + stateful_var_def + other_func_def + "void func(struct Packet p) {\n" + body_part + "\n}";
    } else if (isa<VarDecl>(decl) || isa<RecordDecl>(decl)) {
      if (isa<VarDecl>(decl)){
        //Pay special attention to the definition without initialization
        if (clang_decl_printer(decl).find('=') == std::string::npos){
          std::string stateful_var_name; //stateful_var_name store the name which should appear in the definition part
          std::map<std::string,std::string>::iterator it;
          it = c_to_sk.find(dyn_cast<VarDecl>(decl)->getNameAsString());
          if (it != c_to_sk.end()){
            stateful_var_name = c_to_sk[dyn_cast<VarDecl>(decl)->getNameAsString()];
          }else{
            stateful_var_name = dyn_cast<VarDecl>(decl)->getNameAsString();
          }
          stateful_var_def += ("int " + stateful_var_name + ";\n");
          continue;
        }
        //dyn_cast<VarDecl>(decl)->getDefinition() gets the var_name i.e int count = 0; --> count
        std::string var_name = clang_value_decl_printer(dyn_cast<VarDecl>(decl)->getDefinition());
        //dyn_cast<VarDecl>(decl)->getInit() get initial value i.e int count = 0; --> the initializer is 0
        std::string init_val = clang_stmt_printer(dyn_cast<VarDecl>(decl)->getInit());
        //TODO: to see whether the stateful_var is an array or not
        std::size_t found = init_val.find('{');
        if (found != std::string::npos){
          var_name += '[';
        }
        //TODO: to see whether this stateful_vars has appeared in the function body
        std::string stateful_var_name; //stateful_var_name store the name which should appear in the definition part
        std::map<std::string,std::string>::iterator it;
        it = c_to_sk.find(var_name);
        if (it != c_to_sk.end()){
          stateful_var_name = c_to_sk[var_name];
        }else{
          stateful_var_name = var_name;
        }
        //Return the result ## need some special help for the array_vars
        stateful_var_def += ("int " + stateful_var_name + " = " + init_val +";\n");
      }else if (isa<RecordDecl>(decl)){
        //dyn_cast<RecordDecl>(decl)->getNameAsString() get the name of the struct
        struct_def += "struct " + dyn_cast<RecordDecl>(decl)->getNameAsString() + "{\n";  
        for (const auto * field_decl : dyn_cast<DeclContext>(decl)->decls()){
          std::string pkt_vars = dyn_cast<FieldDecl>(field_decl)->getNameAsString();
          //TODO: to see whether this stateless_vars has appeared in the function body
          std::string stateless_var_name; //stateful_var_name store the name which should appear in the definition part
          std::map<std::string,std::string>::iterator it;
          it = c_to_sk.find(pkt_vars);
          if (it != c_to_sk.end()){
            stateless_var_name = c_to_sk[pkt_vars];
          }else{
            stateless_var_name = pkt_vars;
          }
          //dyn_cast<FieldDecl>(field_decl)->getNameAsString() get the name of pkt_vars
          struct_def += "    int " + stateless_var_name + ";\n";
        }
        struct_def += "};\n";
      }
    } else if ((isa<FunctionDecl>(decl) and
                (not is_packet_func(dyn_cast<FunctionDecl>(decl))))) {
      other_func_def += clang_decl_printer(decl);
    }
  }

  assert_exception(false);
}

std::string RenameDominoCodeGenerator::ast_visit_decl_ref_expr(
    const clang::DeclRefExpr *decl_ref_expr) {
  assert_exception(decl_ref_expr);
  std::string s = clang_stmt_printer(decl_ref_expr);
  std::map<std::string, std::string>::iterator it;
  it = c_to_sk.find(s);
  if (it == c_to_sk.end()) {
    std::string name;
    // stateless
    if (s.find('.') != std::string::npos) {
      // Should never get here.
      assert_exception(false);
    } else {
      name = "state_" + std::to_string(count_stateful);
      count_stateful++;
      c_to_sk[s] = name;
    }
  }
  return c_to_sk[s];
}

std::string RenameDominoCodeGenerator::ast_visit_member_expr(
    const clang::MemberExpr *member_expr) {
  assert_exception(member_expr);
  std::string s = clang_stmt_printer(member_expr);
  // TODO p.src -> src
  s = s.substr(s.find('.') + 1);
  std::map<std::string, std::string>::iterator it;
  it = c_to_sk.find(s);
  if (it == c_to_sk.end()) {
    std::string name;
    // stateless
    if (s.find('[') == std::string::npos) {
      name = "pkt_" + std::to_string(count_stateless);
      count_stateless++;
    } else {
      // Should never get here.
      assert_exception(false);
    }
    c_to_sk[s] = name;
  }
  return "p." + c_to_sk[s];
}

std::string RenameDominoCodeGenerator::ast_visit_array_subscript_expr(
    const clang::ArraySubscriptExpr *array_subscript_expr) {
  assert_exception(array_subscript_expr);
  std::string s = clang_stmt_printer(array_subscript_expr);

  std::map<std::string, std::string>::iterator it;
  std::string name_in_origin_program = s.substr(0, s.find('[') + 1);
  it = c_to_sk.find(name_in_origin_program);
  if (it == c_to_sk.end()) {
    std::string name;
    if (s.find('[') == std::string::npos) {
      // Should never get here.
      assert_exception(false);
    } else {
      name = "state_" + std::to_string(count_stateful);
      count_stateful++;
      c_to_sk[name_in_origin_program] = name;
      return name;
    }
  }
  return c_to_sk[name_in_origin_program];
}

void RenameDominoCodeGenerator::print_map() {
  if (c_to_sk.size() == 0 )
    return;
  std::cout << "// Print map" << std::endl;
  for(std::map<std::string, std::string >::const_iterator it = c_to_sk.begin();
    it != c_to_sk.end(); ++it){
    std::cout << "//" << it->first << " = " << it->second << "\n";
  }
  std::cout << std::endl;  
}
