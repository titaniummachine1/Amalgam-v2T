---
trigger: always_on
---
# Google C++ Style Guide Rules for Windsurf
# Reference: https://google.github.io/styleguide/cppguide.html

rules:
  - name: cpp_formatting_basics
    description: Fundamental formatting requirements for Google C++ Style
    patterns: ["**/*.cpp", "**/*.cc", "**/*.h", "**/*.hpp"]
    instructions: |
      - Use 2 spaces for indentation. Never use tabs.
      - Limit line length to 80 characters.
      - Ensure every .cc file has an associated .h file (unless it's a main() or unit test).
      - Use UTF-8 encoding for all files.
      - No trailing whitespace at the end of lines.

  - name: cpp_naming_conventions
    description: Naming rules for variables, functions, and types
    patterns: ["**/*.cpp", "**/*.cc", "**/*.h", "**/*.hpp"]
    instructions: |
      - File names: All lowercase, underscores preferred (e.g., `my_useful_class.cc`).
      - Type names (Classes, Structs, Enums, Typedefs): MixedCase starting with upper (e.g., `MyExcitingClass`).
      - Variable names: All lowercase with underscores (e.g., `local_variable`, `my_class_member_`).
      - Class data members: Must end with a trailing underscore (e.g., `name_`).
      - Constant names: Start with a 'k' followed by MixedCase (e.g., `kDaysInAWeek`).
      - Function names: MixedCase starting with upper (e.g., `MyFunction()`).
      - Enumerator names: Named like Constants (e.g., `kEnumName`).

  - name: cpp_header_guards
    description: Format for header guards
    patterns: ["**/*.h", "**/*.hpp"]
    instructions: |
      - All headers must use #define guards.
      - Format: <PROJECT>_<PATH>_<FILE>_H_
      - Example for `foo/src/bar/baz.h`:
        #ifndef FOO_BAR_BAZ_H_
        #define FOO_BAR_BAZ_H_
        ...
        #endif // FOO_BAR_BAZ_H_

  - name: cpp_language_restrictions
    description: Best practices for C++ features
    patterns: ["**/*.cpp", "**/*.cc", "**/*.h", "**/*.hpp"]
    instructions: |
      - Target C++20 features; avoid C++23 or non-standard extensions.
      - Avoid using forward declarations; #include the header instead.
      - Use `std::unique_ptr` and `std::shared_ptr` for ownership management.
      - Do not use `using namespace std;`.
      - Use `nullptr` instead of `NULL` or `0`.
      - Prefer `sizeof(variable_name)` over `sizeof(type)`.

  - name: cpp_class_structure
    description: Rules for class declarations
    patterns: ["**/*.h", "**/*.hpp"]
    instructions: |
      - Use the order: public:, then protected:, then private:.
      - Indent access modifiers (public, private) by 1 space.
      - Do not leave a blank line after the access modifier keyword.
      - Use `explicit` for constructors that can be called with a single argument.
      - Avoid complex logic in constructors; use an `Init()` method if necessary.

  - name: cpp_comment_style
    description: Requirements for documentation and implementation comments
    patterns: ["**/*.cpp", "**/*.cc", "**/*.h", "**/*.hpp"]
    instructions: |
      - Use `//` for all comments (Google style prefers this over `/* */`).
      - Put 2 spaces between code and a trailing comment.
      - Every file should have a boilerplate/copyright comment at the top.
      - Use `TODO(username): description` for temporary code.