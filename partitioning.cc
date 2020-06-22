#include "partitioning.h"

#include "third_party/assert_exception.h"

#include "util.h"
#include "clang_utility_functions.h"
#include "unique_identifiers.h"
#include "set_idioms.h"

using namespace clang;

typedef std::map<std::string, std::vector<std::string>> s_to_vec_map;

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

bool is_state_var(const std::string &s) {
    if (find(s.begin(), s.end(), '.') == s.end() or
        (find(s.begin(), s.end(), '.') != s.end() and find(s.begin(), s.end(), '[') != s.end())) {
      return true;
    } else {
      return false;
    }
}

bool char_is_num(char c) {
  if (c >= '0' && c <= '9') {
    return true;
  } else {
    return false;
  }
}

bool is_pkt_field(const std::string &s) {
    if (find(s.begin(), s.end(), '.') != s.end() and 
         (s.find_first_of("0123456789") == std::string::npos or
         !char_is_num(s.back()))) {
      return true;
    } else {
      return false;
    }
}

// Remove the num char in tail at most twice
std::string remove_num_in_str(std::string s) {
  assert (s.length() > 0);
  int count = 0;
  while (s.back() >= '0' and s.back() <= '9' and count < 2) {
    count++;
    s.pop_back();
  }
  return s;
}

std::string get_last_num_str(std::string s) {
  std::string ret_str = "";
  for (unsigned long i = s.length() - 1; i >= 0; i--) {
    if (char_is_num(s[i])) {
      ret_str.insert(ret_str.begin(), s[i]);
    } else {
      break;
    }
  }
  return ret_str;
}

// turn filter1[pkt.filter1_idx00] into filter1[pkt.filter1_idx]
std::string remove_num_in_index(std::string s) {
    std::string index_str = s.substr(s.find('[') + 1, s.find(']') - s.find('[') - 1);
    index_str = remove_num_in_str(index_str);
    return s.substr(0, s.find('[') + 1) + index_str + "]";
}

void output_to_file(s_to_vec_map &influ_map) {
  s_to_vec_map::iterator it;
  std::string influence_str = "";
  for (it = influ_map.begin(); it != influ_map.end(); it++) {
    std::string first_str;
    if (it->first.back() == ']' and char_is_num(it->first[it->first.length() - 2])) {
       // Get substr index
       first_str = remove_num_in_index(it->first);
    } else {
       first_str = it->first;
    }
    influence_str += first_str + ":";
    for (uint32_t i = 0; i < it->second.size(); i++) {
      std::string second_str;
      if (it->second[i].back() == ']' and char_is_num(it->second[i][it->second[i].length() - 2])) {
         second_str = remove_num_in_index(it->second[i]);
      } else {
         second_str = it->second[i];
      }
      if (i == it->second.size() - 1) {
          influence_str += second_str + "\n";
      } else {
          influence_str += second_str + ",";
      }
    }
  }
  std::string output_filename = "/tmp/influence_map.txt";
  std::ofstream myfile;
  myfile.open(output_filename.c_str());
  myfile << influence_str;
  myfile.close();
}

void print_map(s_to_vec_map &dep_map) {
  std::cout << "start printing the map: " << std::endl;
  s_to_vec_map::iterator it;
  for (it = dep_map.begin(); it != dep_map.end(); it++) {
    std::cout << "it->first(key) = " << it->first;
    std::cout << "  it->second(val) = ";
    for (uint32_t i = 0; i < it->second.size(); i++) {
      std::cout << it->second[i] << ",";
    }
    std::cout << std::endl;
  }
}

// Get the real influencers
void simplify_map(s_to_vec_map &ret_map) {
  std::vector<std::string> lhs_vec;
  std::vector<std::string> state_var_vec;
  // get the pkt_field name
  std::vector<std::string> pkt_field_vec;
  // Fill in lhs_vec
  s_to_vec_map::iterator it;
  for (it = ret_map.begin(); it != ret_map.end(); it++) {
    lhs_vec.push_back(it->first);
  }
  // Add new members which only appear in RHS
  for (it = ret_map.begin(); it != ret_map.end(); it++) {
    for (uint32_t i = 0; i < it->second.size(); i++) {
      if (find(lhs_vec.begin(), lhs_vec.end(), it->second[i]) == lhs_vec.end()) {
        ret_map[it->second[i]] = {it->second[i]};
      }
    }
  }

  // Which member should be stored in remain vec (rem_vec)
  std::vector<std::string> rem_vec;
  // Get all state_var members and pkt_fields in LHS
  for (it = ret_map.begin(); it != ret_map.end(); it++) {
    if (is_state_var(it->first)) {
      rem_vec.push_back(it->first);
    } else if (is_pkt_field(it->first)) {
      pkt_field_vec.push_back(it->first);
    }
  }

  // Fill in pkt_fields again
  // ex. p.now_plus_free0: p.now_plus_free0 p.now
  for (it = ret_map.begin(); it != ret_map.end(); it++) {
    if (is_state_var(it->first)) {
      continue;
    }
    int flag = 0;
    for (uint32_t i = 0; i < pkt_field_vec.size(); i++) {
      // tmp to store pkt fields
      if (it->first.find(pkt_field_vec[i]) != std::string::npos and
          it->first.length() > pkt_field_vec[i].length() and
          char_is_num(it->first[pkt_field_vec[i].length()])) {
        flag = 1;
        break;
      }
    }
    for (uint32_t i = 0; i < rem_vec.size(); i++) {
      // tmp to store stateful var
      if (it->first.find(rem_vec[i]) != std::string::npos) {
        flag = 1;
        break;
      }
    }
    if (flag == 0) {
      // remove the latter num
      std::string new_pkt_str = remove_num_in_str(it->first);
      int add_flag = 1;
      for (uint32_t i = 0; i < rem_vec.size(); i++) {
        if (is_state_var(rem_vec[i]) and rem_vec[i].find('[') != std::string::npos) {
          std::string match_str = rem_vec[i].substr(0, rem_vec[i].find('['));
          std::size_t pos = new_pkt_str.find(match_str);
          if (pos != std::string::npos) {
            if (pos + match_str.length() == new_pkt_str.length()) {
              add_flag = 0;
              break;
            }
          }
        }
      }
      if (add_flag == 1 && find(pkt_field_vec.begin(), pkt_field_vec.end(), new_pkt_str) == pkt_field_vec.end()) {
        pkt_field_vec.push_back(new_pkt_str);
      }
    }
  }

  // TODO: build a map to store how many members in LHS
  for (uint32_t i = 0; i < pkt_field_vec.size(); i++) {
    int curr_val = -2;
    std::string tmp_num_str;
    for (it = ret_map.begin(); it != ret_map.end(); it++) {
      // Do not need to care about stateful var
      if (is_state_var(it->first)) {
        continue;
      }
      // Only care if the name is pkt.field + "num"
      if (it->first.find(pkt_field_vec[i]) != std::string::npos) {
        if (it->first.length() == pkt_field_vec[i].length()) {
          // This means they have the same name
          curr_val = -1;
        } else {
          assert(it->first.length() > pkt_field_vec[i].length());
          // To avoid the case where p.now and p.now_plus_free are two pkt fields in program
          if (char_is_num(it->first[pkt_field_vec[i].length()])) {
             // we should record the number allocated to that pkt field
             curr_val = 3;
             tmp_num_str = get_last_num_str(it->first);
          }
        }
      }
    }
    if (curr_val != -2) {
      if (curr_val == -1) {
        rem_vec.push_back(pkt_field_vec[i]);
      } else  {
        rem_vec.push_back(pkt_field_vec[i] + tmp_num_str);
      } 
    }
  }
  
  std::vector<std::string> erase_vec;
  for (it = ret_map.begin(); it != ret_map.end(); it++) {
    if (find(rem_vec.begin(), rem_vec.end(), it->first) == rem_vec.end()) {
      erase_vec.push_back(it->first);
    }
  }
  for (uint32_t i = 0; i < erase_vec.size(); i++) {
    ret_map.erase(erase_vec[i]);
  }

  s_to_vec_map tmp_map;
  std::vector<std::string> erase_key_vec;
  // Only remain the variables appearing in the program
  for (it = ret_map.begin(); it != ret_map.end(); it++) {
    if (!is_state_var(it->first) and char_is_num(it->first.back())) {
      std::string new_key = remove_num_in_str(it->first);
      for (uint32_t i = 0; i < it->second.size();) {
        if (!is_state_var(it->second[i]) and char_is_num(it->second[i].back())) {
          it->second.erase(it->second.begin() + i);
        } else {
          i++;
        }
      }
      tmp_map[new_key] = it->second;
      erase_key_vec.push_back(it->first);
    } else  {
      for (uint32_t i = 0; i < it->second.size();) {
        if (!is_state_var(it->second[i]) and char_is_num(it->second[i].back())) {
          it->second.erase(it->second.begin() + i);
        } else {
          i++;
        }
      }
    }
  }

  // Remove the members stored in the erase_key_vec
  for (uint32_t i = 0; i < erase_key_vec.size(); i++) {
    ret_map.erase(erase_key_vec[i]);
  }
  // Add members stored in tmp_map
  for (it = tmp_map.begin(); it != tmp_map.end(); it++) {
    ret_map[it->first] = it->second;
  }

  // Add the rest members stored in pkt_field_vec
  for (it = ret_map.begin(); it != ret_map.end(); it++) {
    std::vector<std::string>::iterator itr = find(pkt_field_vec.begin(), pkt_field_vec.end(), it->first);
    if (itr != pkt_field_vec.end()) {
      pkt_field_vec.erase(itr);
    }
  }
  for (uint32_t i = 0; i < pkt_field_vec.size(); i++) {
    ret_map[pkt_field_vec[i]] = {pkt_field_vec[i]};
  }
}


// Get the map that shows which var influence our target var
s_to_vec_map get_influence(s_to_vec_map dependency_map) {
    s_to_vec_map ret_map = dependency_map;
    s_to_vec_map::iterator it;
    for (it = ret_map.begin(); it != ret_map.end(); it++) {
        // No need to deal with tmp vars
        if (it->first.find(".tmp") == std::string::npos) {
            it->second.insert(it->second.begin(), it->first);
            bool flag = 1;
            while (flag != 0) {
              flag = 0;
              for (uint32_t i = 1; i < it->second.size(); ) {
                if (is_state_var(it->second[i])) {
                  i++;
                  continue;
                }
                if (dependency_map[it->second[i]].size() == 0) {
                  if (it->second[i].find(".tmp") != std::string::npos) {
                    it->second.erase(it->second.begin() + i);
                  } else {
                    i++;
                  }
                } else {
                   for (uint32_t j = 0; j < dependency_map[it->second[i]].size(); j++) {
                     if (find(it->second.begin(), it->second.end(), dependency_map[it->second[i]][j]) == it->second.end() 
                         and dependency_map[it->second[i]][j] != it->first) {
                        it->second.push_back(dependency_map[it->second[i]][j]);
                        flag = 1;
                     }
                   }
                   it->second.erase(it->second.begin() + i);
                }
              }
            }
       }
    }
    // Erase tmp in ret_map
    std::vector<std::string> erase_vec;
    for (it = ret_map.begin(); it != ret_map.end(); it++) { 
      if (it->first.find(".tmp") != std::string::npos) {
        erase_vec.push_back(it->first);
      }
    }

    for (uint32_t i = 0; i < erase_vec.size(); i++) {
      ret_map.erase(erase_vec[i]);
    }
    simplify_map(ret_map);
    return ret_map;
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
  // Dependency map store all dependencies
  s_to_vec_map dependency_map;
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
              // if child_str is a number, we can ignore it
              if (!is_number(child_str)) {
                  // if child_str is not in the rhs_vec, we should add it in
                  if (find(rhs_var_vec.begin(), rhs_var_vec.end(), child_str) == rhs_var_vec.end()) {
                       rhs_var_vec.push_back(child_str);
                  }
                  // if child_str is not in the dependency_map[lhs_str] vec, then we should add it in
                  if (find(dependency_map[lhs_str].begin(), dependency_map[lhs_str].end(), child_str) == dependency_map[lhs_str].end()) {
                       dependency_map[lhs_str].push_back(child_str);
                  }
              }
          }
      }
      codelets_for_drawing[stage_id][codelet_id] = codelet_body;
      codelet_id++;
      num_codelets++;
      max_codelet_id = std::max(max_codelet_id, codelet_id);
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
  // influ_map: key is stateful var/pkt field
  //            val is stateful vars and pkt fields that influence its key
  s_to_vec_map influ_map = get_influence(dependency_map);
  output_to_file(influ_map); 
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
