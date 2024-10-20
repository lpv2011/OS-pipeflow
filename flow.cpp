#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <string>
#include <vector>
#include <sstream>
#include <map>
#include <cstring>
#include <fstream>
#include <cctype>

using namespace std;

#define MAX_CMD_LENGTH 256
#define BUFFER_SIZE 4096

struct Node {
    string name;
    string command;
};

struct PipeNode {
    string name;
    string from;
    string to;
};

struct Concatenate {
    string name;
    vector<string> parts;
};

map<string, Node> nodes;
map<string, PipeNode> pipes;
map<string, Concatenate> concats;
map<string, string> stderr_nodes;
map<string, string> files;

vector<string> split_command(const string& command) {
    vector<string> args;
    string current_arg;
    bool in_single_quote = false;
    bool in_double_quote = false;

    for (size_t i = 0; i < command.length(); ++i) {
        char c = command[i];
        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
        } else if (c == '\"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        } else if (isspace(c) && !in_single_quote && !in_double_quote) {
            if (!current_arg.empty()) {
                args.push_back(current_arg);
                current_arg.clear();
            }
        } else {
            current_arg += c;
        }
    }

    if (!current_arg.empty()) {
        args.push_back(current_arg);
    }

    return args;
}

void execute_command(const string& command) {
    vector<string> args = split_command(command);

    if (args.empty()) {
        cerr << "Error: Empty command" << endl;
        exit(1);
    }

    vector<char*> c_args;
    for (auto& arg : args) {
        c_args.push_back(const_cast<char*>(arg.c_str()));
    }
    c_args.push_back(nullptr);

    execvp(c_args[0], c_args.data());
    perror("execvp failed");
    exit(1);
}

string run_command(const string& command) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(1);
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(1);
    }

    if (pid == 0) { 
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execute_command(command);
    } else { 
        close(pipefd[1]);
        char buffer[BUFFER_SIZE];
        string result;
        ssize_t bytes_read;
        while ((bytes_read = read(pipefd[0], buffer, BUFFER_SIZE)) > 0) {
            result.append(buffer, bytes_read);
        }
        close(pipefd[0]);
        waitpid(pid, nullptr, 0);
        return result;
    }
    return "";
}

string execute_stderr_node(const string& node_name) {
    if (nodes.find(node_name) == nodes.end()) {
        cerr << "Error: Unknown node name '" << node_name << "'" << endl;
        exit(1);
    }
    string command = nodes[node_name].command;

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(1);
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(1);
    }

    if (pid == 0) { 
        close(pipefd[0]); 
        dup2(pipefd[1], STDERR_FILENO); 
        close(pipefd[1]);
        execute_command(command);
    } else {  
        close(pipefd[1]); 
        char buffer[BUFFER_SIZE];
        string result;
        ssize_t bytes_read;
        while ((bytes_read = read(pipefd[0], buffer, BUFFER_SIZE)) > 0) {
            result.append(buffer, bytes_read);
        }
        close(pipefd[0]);
        waitpid(pid, nullptr, 0);
        return result;
    }
    return "";
}

string execute_pipe(const PipeNode& pipeNode);
string execute_concat(const Concatenate& concat);

string execute_part(const string& part) {
    if (nodes.find(part) != nodes.end()) {
        return run_command(nodes[part].command);
    } else if (pipes.find(part) != pipes.end()) {
        return execute_pipe(pipes[part]);
    } else if (concats.find(part) != concats.end()) {
        return execute_concat(concats[part]);
    } else if (stderr_nodes.find(part) != stderr_nodes.end()) {
        return execute_stderr_node(stderr_nodes[part]);
    } else if (files.find(part) != files.end()) {
        ifstream infile(files[part]);
        if (!infile.is_open()) {
            cerr << "Error opening file: " << files[part] << endl;
            exit(1);
        }
        stringstream buffer;
        buffer << infile.rdbuf();
        infile.close();
        return buffer.str();
    } else {
        cerr << "Error: Unknown part name '" << part << "'" << endl;
        exit(1);
    }
}

string execute_pipe(const PipeNode& pipeNode) {
    string from_output;

    if (files.find(pipeNode.from) != files.end()) {
        ifstream infile(files[pipeNode.from]);
        if (!infile.is_open()) {
            cerr << "Error opening file: " << files[pipeNode.from] << endl;
            exit(1);
        }
        stringstream buffer;
        buffer << infile.rdbuf();
        infile.close();
        from_output = buffer.str();
    } else {
        from_output = execute_part(pipeNode.from);
    }

    if (files.find(pipeNode.to) != files.end()) {
        ofstream outfile(files[pipeNode.to]);
        if (!outfile.is_open()) {
            cerr << "Error opening file for writing: " << files[pipeNode.to] << endl;
            exit(1);
        }
        outfile << from_output;
        outfile.close();
        return ""; 
    }

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(1);
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(1);
    }

    if (pid == 0) { 
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        int input_pipe[2];
        if (pipe(input_pipe) == -1) {
            perror("pipe");
            exit(1);
        }

        pid_t input_pid = fork();
        if (input_pid == -1) {
            perror("fork");
            exit(1);
        }

        if (input_pid == 0) {
            close(input_pipe[0]);
            write(input_pipe[1], from_output.c_str(), from_output.size());
            close(input_pipe[1]);
            exit(0);
        } else {
            close(input_pipe[1]);
            dup2(input_pipe[0], STDIN_FILENO);
            close(input_pipe[0]);

            if (nodes.find(pipeNode.to) != nodes.end()) {
                execute_command(nodes[pipeNode.to].command);
            } else if (concats.find(pipeNode.to) != concats.end()) {
                string concat_result = execute_concat(concats[pipeNode.to]);
                cout << concat_result;
                exit(0);
            } else if (pipes.find(pipeNode.to) != pipes.end()) {
                string pipe_result = execute_pipe(pipes[pipeNode.to]);
                cout << pipe_result;
                exit(0);
            } else if (stderr_nodes.find(pipeNode.to) != stderr_nodes.end()) {
                string stderr_result = execute_stderr_node(stderr_nodes[pipeNode.to]);
                cout << stderr_result;
                exit(0);
            } else if (files.find(pipeNode.to) != files.end()) {
                ofstream outfile(files[pipeNode.to]);
                if (!outfile.is_open()) {
                    cerr << "Error opening file for writing: " << files[pipeNode.to] << endl;
                    exit(1);
                }
                outfile << from_output;
                outfile.close();
                exit(0);
            } else {
                cerr << "Error: Unknown flow name '" << pipeNode.to << "'" << endl;
                exit(1);
            }
        }
    } else {
        close(pipefd[1]);
        char buffer[BUFFER_SIZE];
        string result;
        ssize_t bytes_read;
        while ((bytes_read = read(pipefd[0], buffer, BUFFER_SIZE)) > 0) {
            result.append(buffer, bytes_read);
        }
        close(pipefd[0]);
        waitpid(pid, nullptr, 0);
        return result;
    }
    return "";
}

string execute_concat(const Concatenate& concat) {
    string result;
    for (const auto& part : concat.parts) {
        result += execute_part(part);
    }
    return result;
}

void read_flow_file(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Error opening flow file" << endl;
        exit(1);
    }

    string line;
    Node currentNode;
    PipeNode currentPipe;
    Concatenate currentConcat;
    bool readingConcat = false;
    string currentFileName;

    while (getline(file, line)) {
        line.erase(line.find_last_not_of("\r\n") + 1);

        if (line.empty()) {
            continue;
        } else if (line.find("node=") == 0) {
            currentNode = Node();
            currentNode.name = line.substr(5);
        } else if (line.find("command=") == 0) {
            currentNode.command = line.substr(8);
            nodes[currentNode.name] = currentNode;
        } else if (line.find("file=") == 0) {
            currentFileName = line.substr(5);
        } else if (line.find("name=") == 0) {
            if (!currentFileName.empty()) {
                string fileName = line.substr(5);
                files[currentFileName] = fileName;
                currentFileName.clear();
            } else {
                cerr << "Error: 'name=' found without preceding 'file='" << endl;
                exit(1);
            }
        } else if (line.find("stderr=") == 0) {
            string stderr_name = line.substr(7);
            getline(file, line);
            if (line.find("from=") == 0) {
                string from_node = line.substr(5);
                stderr_nodes[stderr_name] = from_node;
            }
        } else if (line.find("pipe=") == 0) {
            currentPipe = PipeNode();
            currentPipe.name = line.substr(5);
        } else if (line.find("from=") == 0) {
            currentPipe.from = line.substr(5);
        } else if (line.find("to=") == 0) {
            currentPipe.to = line.substr(3);
            pipes[currentPipe.name] = currentPipe;
        } else if (line.find("concatenate=") == 0) {
            currentConcat = Concatenate();
            currentConcat.name = line.substr(12);
            readingConcat = true;
        } else if (readingConcat && line.find("part_") == 0) {
            size_t equal_pos = line.find('=');
            if (equal_pos != string::npos) {
                string part_name = line.substr(equal_pos + 1);
                currentConcat.parts.push_back(part_name);
            }
        } else if (readingConcat && (line == "end_concatenate" || line.find("concatenate=") == 0)) {
            concats[currentConcat.name] = currentConcat;
            readingConcat = false;

            if (line.find("concatenate=") == 0) {
                currentConcat = Concatenate();
                currentConcat.name = line.substr(12);
                readingConcat = true;
            }
        }
    }

    if (readingConcat) {
        concats[currentConcat.name] = currentConcat;
    }

    file.close();
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <flow_file> <flow_name>" << endl;
        return 1;
    }

    string flow_file = argv[1];
    string flow_name = argv[2];

    read_flow_file(flow_file);

    if (pipes.find(flow_name) != pipes.end()) {
        string output = execute_pipe(pipes[flow_name]);
        cout << output;
    }
  
    else if (concats.find(flow_name) != concats.end()) {
        string output = execute_concat(concats[flow_name]);
        cout << output;
    }

    else if (nodes.find(flow_name) != nodes.end()) {
        string output = run_command(nodes[flow_name].command);
        cout << output;
    }

    else if (stderr_nodes.find(flow_name) != stderr_nodes.end()) {
        string output = execute_stderr_node(stderr_nodes[flow_name]);
        cout << output;
    } else {
        cerr << "Error: Unknown flow name '" << flow_name << "'" << endl;
        return 1;
    }

    return 0;
}
