Project {
    id: root

    property string name
    property string uri
    property string projectFile
    property path sourceCache

    OnlineSource {
        id: probe
        name: root.name
        uri: root.uri
        projectFile: root.projectFile
        sourceCache: root.sourceCache
        buildDirectory: root.buildDirectory
    }
    SubProject {
        condition: probe.found
        filePath: probe.projectFilePath
    }
}
