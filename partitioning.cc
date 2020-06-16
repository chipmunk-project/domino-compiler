#include "partitioning.h"

#include "third_party/assert_exception.h"

#include "util.h"
#include "clang_utility_functions.h"
#include "unique_identifiers.h"
#include "set_idioms.h"

using namespace clang;

// reference website:https://stackoverflow.com/questions/4654636/how-to-determine-if-a-string-is-a-number-with-c
// to see whether a string only consists of numbers
bool is_number(const std::string& s) {
    std::string::const_iterator it = s.begin();
    while (it != s.end() && std::isdigit(*it)) ++it;
    return !s.empty() && it == s.end();
}

void remove_parenthesis(std::string& s) {
    s.erase(s.begin());
    s.erase(s.end() - 1);
}

std::string inst_block_printer(const InstBlock & iblock) {
  std::string ret = "";
  assert_exception(not iblock.empty());
  for (auto & op : iblock) ret += clang_stmt_printer(op) + ";\n";
  return ret;
}

Graph<const BinaryOperator *> handle_state_vars(const std::vector<const BinaryOperator *> & stmt_vector, const Graph<const BinaryOperator*> & dep_graph) {
  Graph<const BinaryOperator*> ret = dep_graph;
  std::map<std::string, const BinaryOperator *> state_reads;
  std::map<std::string, const BinaryOperator *> state_writes;
  for (const auto * stmt : stmt_vector) {
    const auto * lhs = stmt->getLHS()->IgnoreParenImpCasts();
    const auto * rhs = stmt->getRHS()->IgnoreParenImpCasts();
    // At this stage, after stateful_flanks has been run, the only
    // way state variables (scalar or array-based) appear is either on the LHS or on the RHS
    // and they appear by themselves (not as part of another expression)
    // Which is why we don't need to recursively traverse an AST to check for state vars
    if (isa<DeclRefExpr>(rhs) or isa<ArraySubscriptExpr>(rhs)) {
      // Should see exactly one read to a state variable
      assert_exception(state_reads.find(clang_stmt_printer(rhs)) == state_reads.end());
      state_reads[clang_stmt_printer(rhs)] = stmt;
    } else if (isa<DeclRefExpr>(lhs) or isa<ArraySubscriptExpr>(lhs)) {
      // Should see exactly one write to a state variable
      assert_exception(state_writes.find(clang_stmt_printer(lhs)) == state_writes.end());
      state_writes[clang_stmt_printer(lhs)] = stmt;
      const auto state_var = clang_stmt_printer(lhs);
      // Check state_var exists in both maps
      assert_exception(state_reads.find(state_var) != state_reads.end());
      assert_exception(state_writes.find(state_var) != state_writes.end());
      ret.add_edge(state_reads.at(state_var), state_writes.at(state_var));
      ret.add_edge(state_writes.at(state_var), state_reads.at(state_var));
    }
  }

  // Check that there are pairs of reads and writes for every state variable
  for (const auto & pair : state_reads) {
    if (state_writes.find(pair.first) == state_writes.end()) {
      throw std::logic_error(pair.first + " has a read that isn't paired with a write ");
    }
  }
  return ret;
}

bool op_reads_var(const BinaryOperator * op, const Expr * var) {
  assert_exception(op);
  assert_exception(var);

  // We only check packet variables here because handle_state_vars
  // takes care of state variables.
  auto read_vars = gen_var_list(op->getRHS(), {{VariableType::PACKET, true},
                                               {VariableType::STATE_SCALAR, false},
                                               {VariableType::STATE_ARRAY, false}});

  // If the LHS is an array subscript expression, we need to check inside the subscript as well
  if (isa<ArraySubscriptExpr>(op->getLHS())) {
    const auto * array_op = dyn_cast<ArraySubscriptExpr>(op->getLHS());
    const auto read_vars_lhs = gen_var_list(array_op->getIdx(), {{VariableType::PACKET, true},
                                                                {VariableType::STATE_SCALAR, false},
                                                                {VariableType::STATE_ARRAY, false}});
    read_vars = read_vars + read_vars_lhs;
  }

  return (read_vars.find(clang_stmt_printer(var)) != read_vars.end());
}

bool depends(const BinaryOperator * op1, const BinaryOperator * op2) {
  // If op1 succeeds op2 in program order,
  // return false right away
  if (not (op1->getLocStart() < op2->getLocStart())) {
    return false;
  }

  // op1 writes the same variable that op2 writes (Write After Write)
  if (clang_stmt_printer(op1->getLHS()) == clang_stmt_printer(op2->getLHS())) {
    throw std::logic_error("Cannot have Write-After-Write dependencies in SSA form from " + clang_stmt_printer(op1) + " to " + clang_stmt_printer(op2) + "\n");
  }

  // op1 reads a variable that op2 writes (Write After Read)
  if (op_reads_var(op1, op2->getLHS())) {
    // Make an exception for state variables. There is no way around this.
    // There is no need to add this edge, because handle_state_vars() does
    // this already.
    if (isa<DeclRefExpr>(op2->getLHS()) or isa<ArraySubscriptExpr>(op2->getLHS())) {
      return false;
    } else {
      throw std::logic_error("Cannot have Write-After-Read dependencies in SSA form from " + clang_stmt_printer(op1) +  " to " + clang_stmt_printer(op2) + "\n");
    }
  }

  // op1 writes a variable (LHS) that op2 reads. (Read After Write)
  return (op_reads_var(op2, op1->getLHS()));
}

std::map<uint32_t, std::vector<InstBlock>> generate_partitions(const CompoundStmt * function_body) {
  // Verify that it's in SSA
  if (not is_in_ssa(function_body)) {
    throw std::logic_error("Partitioning will run only after program is in SSA form. This program isn't.");
  }
  // Append to a vector of const BinaryOperator *
  // in order of statement occurence.
  std::vector<const BinaryOperator *> stmt_vector;
  for (const auto * child : function_body->children()) {
    assert_exception(isa<BinaryOperator>(child));
    const auto * bin_op = dyn_cast<BinaryOperator>(child);
    assert_exception(bin_op->isAssignmentOp());
    stmt_vector.emplace_back(bin_op);
  }

  // Dependency graph creation
  Graph<const BinaryOperator *> dep_graph(clang_stmt_printer);
  for (const auto * stmt : stmt_vector) {
    dep_graph.add_node(stmt);
  }

  // Handle state variables specially
  dep_graph = handle_state_vars(stmt_vector, dep_graph);

  // Now add all Read After Write Dependencies, comparing a statement only with
  // a successor statement
  for (uint32_t i = 0; i < stmt_vector.size(); i++) {
    for (uint32_t j = i + 1; j < stmt_vector.size(); j++) {
      if (depends(stmt_vector.at(i), stmt_vector.at(j))) {
        dep_graph.add_edge(stmt_vector.at(i), stmt_vector.at(j));
      }
    }
  }

  // Eliminate nodes with no outgoing or incoming edge
  std::set<const BinaryOperator *> nodes_to_remove;
  for (const auto & node : dep_graph.node_set()) {
    if (dep_graph.pred_map().at(node).empty() and
        dep_graph.succ_map().at(node).empty()) {
      nodes_to_remove.emplace(node);
    }
  }
  for (const auto & node : nodes_to_remove) {
    dep_graph.remove_singleton_node(node);
  }

  std::cerr << dep_graph << std::endl;

  // Condense (https://en.wikipedia.org/wiki/Strongly_connected_component)
  // dep_graph after collapsing strongly connected components into one node
  // Pass a function to order statements within the sccs
  const auto & condensed_graph = dep_graph.condensation([] (const BinaryOperator * op1, const BinaryOperator * op2)
                                                        {return op1->getLocStart() < op2->getLocStart();});

  // Partition condensed graph using critical path scheduling
  const auto & partitioning = condensed_graph.critical_path_schedule();

  // Output partition into valid C code, one for each timestamp
  std::map<uint32_t, std::vector<InstBlock>> codelet_bodies;
  std::vector<std::pair<InstBlock, uint32_t>> sorted_pairs(partitioning.begin(), partitioning.end());
  std::sort(sorted_pairs.begin(), sorted_pairs.end(), [] (const auto & x, const auto & y) { return x.second < y.second; });
  std::for_each(sorted_pairs.begin(), sorted_pairs.end(), [&codelet_bodies] (const auto & pair)
                { if (codelet_bodies.find(pair.second) == codelet_bodies.end()) codelet_bodies[pair.second] = std::vector<InstBlock>();
                  codelet_bodies.at(pair.second).emplace_back(pair.first); });


  // Draw pipeline
  uint32_t max_stage_id = 0;
  uint32_t max_codelet_id  = 0;
  uint32_t num_codelets = 0;
  PipelineDrawing codelets_for_drawing;
  for (const auto & body_pair : codelet_bodies) {
    uint32_t codelet_id = 0;
    const uint32_t stage_id = body_pair.first;
    std::vector<std::string> lhs_var_vec;
    std::vector<std::string> rhs_var_vec;
    for (const auto & codelet_body : body_pair.second) {
      for (uint32_t i = 0; i < codelet_body.size(); i++) {
          std::string lhs_str = clang_stmt_printer(codelet_body[i]->getLHS());
          if (find(lhs_var_vec.begin(), lhs_var_vec.end(), lhs_str) == lhs_var_vec.end()) {
               lhs_var_vec.push_back(lhs_str);
          }
          for (const auto * child : codelet_body[i]->getRHS()->children()) {
              std::string child_str = clang_stmt_printer(child);
              // TODO: find a better way to detect parenthesis and remove them
              if (child_str[0] == '(') {
                   remove_parenthesis(child_str);
              }
              if (!is_number(child_str)) {
                  if (find(rhs_var_vec.begin(), rhs_var_vec.end(), child_str) == rhs_var_vec.end()) {
                       rhs_var_vec.push_back(child_str);
                  }
              }
          }
      }
      codelets_for_drawing[stage_id][codelet_id] = codelet_body;
      max_codelet_id = std::max(max_codelet_id, codelet_id);
      codelet_id++;
      num_codelets++;
    }
    uint32_t num_inherited_var = 0;
    // num_inherited_var is the total num of all variables appearing in the rhs but not in the lhs
    for (uint32_t i = 0; i < rhs_var_vec.size(); i++) {
        if (find(lhs_var_vec.begin(), lhs_var_vec.end(), rhs_var_vec[i]) == lhs_var_vec.end()) {
              num_inherited_var++;
        }
    }
    max_codelet_id = std::max(max_codelet_id, num_inherited_var);
    max_stage_id = std::max(max_stage_id, stage_id);
  }
  std::cerr << draw_pipeline(codelets_for_drawing, condensed_graph) << std::endl;
  std::cout << "Total of " + std::to_string(max_stage_id + 1) + " stages" << std::endl;
  std::cout << "Maximum of " + std::to_string(max_codelet_id) + " codelets/stage" << std::endl;
  std::cout << "Total of " << num_codelets << " codelets" << std::endl;
  return codelet_bodies;
}

std::string draw_pipeline(const PipelineDrawing & codelets_for_drawing, const Graph<InstBlock> & condensed_graph) {
  // Preamble for dot (node shape, fontsize etc)
  std::string ret = "digraph pipeline_diagram {splines=true node [shape = box style=\"rounded,filled\" fontsize = 10];\n";
  const uint32_t scale_x = 250;
  const uint32_t scale_y = 75;

  // Print out nodes
  for (const auto & stageid_with_codelet_map : codelets_for_drawing) {
    const uint32_t stage_id = stageid_with_codelet_map.first;
    for (const auto & codelet_pair : stageid_with_codelet_map.second) {
      const uint32_t codelet_id = codelet_pair.first;
      const auto codelet = codelets_for_drawing.at(stage_id).at(codelet_id);
      const auto codelet_as_str = inst_block_printer(codelet);
      ret += hash_string(codelet_as_str) + " [label = \""
                                      + codelet_as_str + "\""
                                      + "  pos = \""
                                      + std::to_string(scale_x * stage_id) + "," + std::to_string(scale_y * codelet_id) + "\""
                                      + " fillcolor=" + (codelet.size() > 1 ? "darkturquoise" : "white")
                                      + "];\n";
    }
  }

  // Print out edges
  for (const auto & node_pair : condensed_graph.succ_map())
    for (const auto & neighbor : node_pair.second)
      ret += hash_string(inst_block_printer(node_pair.first)) + " -> " +
             hash_string(inst_block_printer(neighbor)) + " ;\n";
  ret += "}";
  return ret;
}

std::string partitioning_transform(const TranslationUnitDecl * tu_decl, const uint32_t pipeline_depth, const uint32_t pipeline_width) {
  const auto & id_set = identifier_census(tu_decl);

  // Storage for returned string
  std::string ret;

  // Create unique identifier generator
  UniqueIdentifiers unique_identifiers(id_set);

  for (const auto * child_decl : dyn_cast<DeclContext>(tu_decl)->decls()) {
    assert_exception(child_decl);
    if (isa<VarDecl>(child_decl) or
        isa<RecordDecl>(child_decl)) {
      // Pass through these declarations as is
      ret += clang_decl_printer(child_decl) + ";";
    } else if (isa<FunctionDecl>(child_decl) and (not is_packet_func(dyn_cast<FunctionDecl>(child_decl)))) {
      ret += generate_scalar_func_def(dyn_cast<FunctionDecl>(child_decl));
    } else if (isa<FunctionDecl>(child_decl) and (is_packet_func(dyn_cast<FunctionDecl>(child_decl)))) {
      const auto * function_decl = dyn_cast<FunctionDecl>(child_decl);

      // Extract function signature
      assert_exception(function_decl->getNumParams() >= 1);
      const auto * pkt_param = function_decl->getParamDecl(0);
      const auto pkt_type  = function_decl->getParamDecl(0)->getType().getAsString();
      const auto pkt_name = clang_value_decl_printer(pkt_param);

      // Transform function body
      const auto codelet_bodies = generate_partitions(dyn_cast<CompoundStmt>(function_decl->getBody()));

      // Create codelet functions with new bodies, encode stage_id and codelet_id within function signature.
      for (const auto & body_pair : codelet_bodies) {
        uint32_t codelet_id = 0;
        const uint32_t stage_id = body_pair.first;
        for (const auto & codelet_body : body_pair.second) {
          const auto codelet_body_as_str = function_decl->getReturnType().getAsString() + " " +
                                        "_codelet_" + std::to_string(stage_id) + "_" + std::to_string(codelet_id) +
                                        "( " + pkt_type + " " +  pkt_name + ") { " +
                                        inst_block_printer(codelet_body) + "}\n";
          codelet_id++;
          ret += codelet_body_as_str;
        }
      }

      // count number of stages in the pipeline and max stage width
      uint32_t max_stage_id = 0;
      uint32_t max_codelet_id  = 0;
      for (const auto & body_pair : codelet_bodies) {
        uint32_t codelet_id = 0;
        const uint32_t stage_id = body_pair.first;
        for (const auto & codelet_body __attribute__ ((unused)) : body_pair.second) {
          max_codelet_id = std::max(max_codelet_id, codelet_id);
          codelet_id++;
        }
        max_stage_id = std::max(max_stage_id, stage_id);
      }
      if ((max_stage_id + 1) > pipeline_depth) {
        const auto diagnostics = "// Pipeline depth of " + std::to_string(max_stage_id + 1) + " exceeds allowed pipeline depth of " + std::to_string(pipeline_depth);
        throw std::logic_error(diagnostics);
      }
      if ((max_codelet_id + 1) > pipeline_width) {
        const auto diagnostics = "// Pipeline width of " + std::to_string(max_codelet_id + 1) + " exceeds allowed pipeline width of " + std::to_string(pipeline_width);
        throw std::logic_error(diagnostics);
      }
    }
  }
  return ret;
}
