#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "cardcode/engine.hpp"
#include "cardcode/mock_robot_host.hpp"

using namespace cardcode;

namespace {

// Prints the event stream and interleaves the mock robot's commands so the
// ROBOT line for each command appears under the node that issued it.
class PrintingSink : public ExecutionEventSink {
public:
    explicit PrintingSink(MockRobotHost& robot) : robot_(robot) {}

    void on_event(const ExecutionEvent& e) override {
        switch (e.type) {
            case ExecutionEventType::ProgramStart:
                std::cout << "ProgramStart\n";
                break;
            case ExecutionEventType::ProgramDone:
                flush_commands();
                std::cout << "ProgramDone\n";
                break;
            case ExecutionEventType::ProgramError:
                flush_commands();
                std::cout << "ProgramError: " << e.message << "\n";
                break;
            case ExecutionEventType::NodeStart:
                std::cout << "NodeStart " << node_id_to_string(e.node_id) << " " << e.message
                          << " bytes=" << e.span.start_offset << ".." << e.span.end_offset << "\n";
                break;
            case ExecutionEventType::NodeDone:
                flush_commands();
                std::cout << "NodeDone " << node_id_to_string(e.node_id) << "\n";
                break;
            case ExecutionEventType::NodeError:
                std::cout << "NodeError " << node_id_to_string(e.node_id) << ": " << e.message << "\n";
                break;
            default:
                break;
        }
    }

private:
    void flush_commands() {
        for (; emitted_ < robot_.commands.size(); ++emitted_) {
            const auto& c = robot_.commands[emitted_];
            std::cout << "  ROBOT " << c.name << " a0=" << c.arg0 << " a1=" << c.arg1 << "\n";
        }
    }

    MockRobotHost& robot_;
    std::size_t emitted_ = 0;
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: cardcode-run <file.ccode>\n";
        return 2;
    }

    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "error: cannot open " << argv[1] << "\n";
        return 2;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string source = ss.str();

    CompileResult compiled = compile(source);
    for (const auto& d : compiled.diagnostics) {
        std::cerr << format_diagnostic(d) << "\n";
    }
    if (!compiled.ok()) {
        std::cerr << "compilation failed\n";
        return 1;
    }

    MockRobotHost robot;
    PrintingSink sink(robot);
    ExecutionResult result = execute(*compiled.root, robot, sink);
    return result.success ? 0 : 1;
}
