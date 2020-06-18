#ifndef COMPILER_PASS_H_
#define COMPILER_PASS_H_

#include <string>
#include <functional>
#include <cstdio>
#include <memory>
#include <sstream>
#include <iostream>
#include <tuple>
#include <experimental/tuple>

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Parse/ParseAST.h"
#include "llvm/Support/Host.h"
#include "llvm/ADT/SmallString.h"

#include "third_party/temp_file.hh"
#include "third_party/assert_exception.h"

#include "clang_utility_functions.h"


/// Convenience typedef for transforming a translation unit
/// This is where all the interesting stuff happens: if conversion, pipelining, etc.
/// TranslationUnitDecl is the clang class for traversing the AST of a parsed translation unit.
/// The return type is a string, because clang doesn't allow you to construct or modify ASTs.
template<typename ...Args>
using Transformer = std::function<std::string(const clang::TranslationUnitDecl *, const Args... t_args)>;

/// Abstract base class for a pass of the Clang compiler,
/// a function object that takes a string corresponding to a translation unit
/// and returns another string as another translation unit.
/// I would have preferred to use a more articulate representation
/// such as an Abstract Syntax Tree to represent the program.
/// Unfortunately, clang has very poor support for creating ASTs. It's
/// best to treat ASTs as intermediate read-only form.
class CompilerPass {
 public:
  /// Run the compiler pass on a string and return a new string
  virtual std::string operator()(const std::string &) = 0;

  /// Virtual destructor to shut up g++
  virtual ~CompilerPass() {};
};

/// Single pass over a translation unit.
/// Most code here is based on
/// http://eli.thegreenplace.net/2012/06/08/basic-source-to-source-transformation-with-clang
template<typename... Args>
class SinglePass  : public CompilerPass {
 public:
  /// Construct a SinglePass using a Transformer object
  SinglePass(const Transformer<Args...> & t_transformer, const Args... t_args);

  /// Execute SinglePass object overriding function call operator
  std::string operator() (const std::string & string_to_parse) final override;

 private:
  class MyASTConsumer : public clang::ASTConsumer {
    public:
     MyASTConsumer(const Transformer<Args...> & t_transformer, const Args... t_args) : transformer_(t_transformer), args_(std::make_tuple(t_args...)) {};

     /// Override the method that gets called for the translation unit
     virtual void HandleTranslationUnit(clang::ASTContext & context) override {
       const auto * tu_decl = context.getTranslationUnitDecl();
       assert_exception(llvm::isa<clang::TranslationUnitDecl>(tu_decl));

       // Bind first argument alone (http://stackoverflow.com/questions/11902840/binding-member-functions-in-a-variadic-fashion)
       auto partial_fn = [tu_decl, this] (const Args... t_args) { return this->transformer_(tu_decl, t_args...); };

       // Now pack the args_ tuple object into a parameter pack (http://en.cppreference.com/w/cpp/apply)
       // Fix from ref: https://stackoverflow.com/questions/57803556/why-i-get-error-apply-is-not-a-member-of-std
       // We cannot directly use std::apply because of g++ version
       output_ = std::experimental::apply(partial_fn, args_);
     }

     /// Get previously stored output
     auto output() const { return output_; }
    private:
     /// Temporary holding area for output
     std::string  output_ = {};

     /// Transformer function
     Transformer<Args...> transformer_;

     /// Temporary storage for arguments
     std::tuple<Args...> args_;
  };
  /// Instantiate MyASTConsumer using supplied transformer
  MyASTConsumer my_ast_consumer_;

  /// SimpleDiag, TODO: Add FixIt hints and other goodies to make it comparable to gcc/clang
  class  SimpleDiag : public clang::DiagnosticConsumer {
    void HandleDiagnostic(clang::DiagnosticsEngine::Level DiagLevel __attribute__((unused)),
                          const clang::Diagnostic & diagnostic) override {
      llvm::SmallString<100> OutStr;
      diagnostic.FormatDiagnostic(OutStr);
      throw std::logic_error("Clang compiler error: " + std::string(OutStr.str()));
    }
  };
  SimpleDiag simple_diag_ = {};

  /// TempFile to hold string to be parsed
  /// This is really a workaround for the fact that the
  /// entry points into clang's libraries are files on disk
  TempFile temp_file_;
};

template<typename... Args>
SinglePass<Args...>::SinglePass(const Transformer<Args...> & t_transformer, const Args... t_args)
    : my_ast_consumer_(t_transformer, t_args...),
      temp_file_("tmp", ".c") {}

template<typename... Args>
std::string SinglePass<Args...>::operator()(const std::string & string_to_parse) {
  // Write string_to_parse into temp_file_
  temp_file_.write(string_to_parse);

  // clang::CompilerInstance will hold the instance of the Clang compiler for us,
  // managing the various objects needed to run the compiler.
  clang::CompilerInstance TheCompInst;
  TheCompInst.getDiagnosticOpts().Warnings.emplace_back("all");
  TheCompInst.createDiagnostics(& simple_diag_, false);
  TheCompInst.getLangOpts().CPlusPlus = 0;
  TheCompInst.getLangOpts().LineComment = 1;

  // Initialize target info with the default triple for our platform.
  auto TO = std::make_shared<clang::TargetOptions>();
  TO->Triple = llvm::sys::getDefaultTargetTriple();
  clang::TargetInfo *TI =
      clang::TargetInfo::CreateTargetInfo(TheCompInst.getDiagnostics(), TO);
  TheCompInst.setTarget(TI);

  TheCompInst.createFileManager();
  clang::FileManager &FileMgr = TheCompInst.getFileManager();
  TheCompInst.createSourceManager(FileMgr);
  clang::SourceManager &SourceMgr = TheCompInst.getSourceManager();
  TheCompInst.createPreprocessor(clang::TU_Module);
  TheCompInst.createASTContext();

  // Set the main file handled by the source manager to the input file.
  const clang::FileEntry *FileIn = FileMgr.getFile(temp_file_.name().c_str());
  SourceMgr.setMainFileID(
      SourceMgr.createFileID(FileIn, clang::SourceLocation(), clang::SrcMgr::C_User));
  TheCompInst.getDiagnosticClient().BeginSourceFile(
      TheCompInst.getLangOpts(), &TheCompInst.getPreprocessor());

  // Parse the file to AST, registering my_ast_consumer_ as the AST consumer.
  ParseAST(TheCompInst.getPreprocessor(), &my_ast_consumer_,
           TheCompInst.getASTContext());

  return my_ast_consumer_.output();
}

// Run a SinglePass repeatedly until the output converges to a fixed point
template <class PassType, class ArgType>
class FixedPointPass : public CompilerPass {
 public:
  /// Construct a FixedPointPass
  FixedPointPass(const ArgType & arg)
      : arg_(arg) {}

  /// Execute FixedPointPass object
  std::string operator() (const std::string & string_to_parse) final override {
    std::string old_output = string_to_parse;
    std::string new_output = "";
    while (true) {
      new_output = PassType(arg_)(old_output);
      if (new_output == old_output) break;
      old_output = new_output;
    }
    return new_output;
  }

 private:
  /// Store argument to pass for future use
  /// TODO: Figure out how to store a parameter pack in the future.
  /// Or maybe don't do something that crazy ...
  ArgType arg_;
};

// Set of passes that are to be run in a particular order
class CompoundPass : public CompilerPass {
 public:
  /// Construct a CompoundPass
  CompoundPass(const std::vector<Transformer<>> & t_transforms)
    : transforms_(t_transforms) {}

  /// Execute CompoundPass object
  std::string operator() (const std::string & string_to_parse) final override {
    std::string output = string_to_parse;
    for (const auto & transform : transforms_) {
      output = SinglePass<>(transform)(output);
    }
    return output;
  }

 private:
  /// Store t_transorms for future use
  std::vector<Transformer<>> transforms_;
};

// Vector of Transform functions, each transform function runs within a CompilerPass
typedef  std::vector<CompilerPass *> TransformVector;

#endif  // COMPILER_PASS_H_
