#include <morphic_syscalls.h>
#include <utils/std.h>
#include <os_event.h>

void strcpy(char* dest, const char* src) {
    while(*src) *dest++ = *src++;
    *dest = 0;
}

// Simple Calculator Logic
class Calculator {
    char display[32];
    int current_val;
    int stored_val;
    char op;
    bool new_entry;

public:
    Calculator() : current_val(0), stored_val(0), op(0), new_entry(true) {
        strcpy(display, "0");
    }

    const char* get_display() { return display; }

    void press_digit(int d) {
        if (new_entry) {
            current_val = d;
            new_entry = false;
        } else {
            current_val = current_val * 10 + d;
        }
        kitoa(current_val, display, 10);
    }

    void press_op(char o) {
        stored_val = current_val;
        op = o;
        new_entry = true;
    }

    void press_equals() {
        if (op == '+') current_val = stored_val + current_val;
        if (op == '-') current_val = stored_val - current_val;
        if (op == '*') current_val = stored_val * current_val;
        if (op == '/') {
            if (current_val != 0) current_val = stored_val / current_val;
            else current_val = 0; // Error
        }
        op = 0;
        new_entry = true;
        kitoa(current_val, display, 10);
    }

    void press_clear() {
        current_val = 0;
        stored_val = 0;
        op = 0;
        new_entry = true;
        strcpy(display, "0");
    }
};

Calculator calc;

int main(int argc, char** argv) {
    sys_debug_print("Calculator App Started! (PID: ?)\n");
    
    while(1) {
        sys_sleep(1000);
        sys_debug_print("Calculator is running...\n");
        
        // Process Input
        OSEvent ev;
        if (sys_input_poll(&ev)) {
            if (ev.type == OSEvent::KEY_PRESS) {
                if (ev.ascii >= '0' && ev.ascii <= '9') {
                    calc.press_digit(ev.ascii - '0');
                    sys_debug_print("Calc Display: ");
                    sys_debug_print(calc.get_display());
                    sys_debug_print("\n");
                }
            }
        }
    }
    
    return 0;
}
