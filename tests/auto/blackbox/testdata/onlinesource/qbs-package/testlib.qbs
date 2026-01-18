// Simple Qbs library project for testing OnlineSource provider
Product {
    name: "testlib"
    type: "staticlibrary"
    
    Depends { name: "cpp" }
    
    files: ["src/testlib.cpp"]
    
    Export {
        Depends { name: "cpp" }
        cpp.includePaths: [product.sourceDirectory + "/include"]
    }
}
