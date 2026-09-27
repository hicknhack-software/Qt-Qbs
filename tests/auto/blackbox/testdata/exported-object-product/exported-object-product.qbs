Project {
    Product {
        name: "objects"
        type: ["obj.exported"]
        Depends { name: "cpp" }
        files: "answer.cpp"
    }
    CppApplication {
        name: "consumer"
        consoleApplication: true
        Depends { name: "objects" }
        files: "main.cpp"
    }
}
