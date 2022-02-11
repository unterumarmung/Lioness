// Forward declare the function to avoid possible bloat in LLVM IR
extern "C" int printf(const char *, ...);

[[lioness::flatten]] int is_zero_or_one(int number) {
    if (number == 0) {
        return true;
    }
    if (number == 1) {
        return true;
    }
    return false;
}

int main(int argc, char **argv) {
    int number = argv[1][0] - '0';
    printf("number is 0 or 1: %d", is_zero_or_one(number));
}
