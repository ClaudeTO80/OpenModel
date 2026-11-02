#include <OpenModel/MeshData.hpp>
#include <OpenModel/MeshValidator.hpp>

int main() {
    MeshData mesh;
    const auto nodeCount = mesh.getNumNodes();
    return nodeCount == 0 ? 0 : 1;
}
