#include <OpenModel/Fatigue/FatigueDamageMapWebViewAdapter.hpp>

#include <cassert>

using namespace OpenModel;
using namespace OpenModel::Fatigue;

int main() {
    MeshData mesh;
    const auto n0 = mesh.addNode(1, 0.0, 0.0, 0.0);
    const auto n1 = mesh.addNode(2, 1.0, 0.0, 0.0);
    const auto n2 = mesh.addNode(3, 0.0, 1.0, 0.0);
    const auto n3 = mesh.addNode(4, 1.0, 1.0, 0.0);
    mesh.addElement("triangles", ElementType::Triangle3, 100, {n0, n1, n2});
    mesh.addElement("triangles", ElementType::Triangle3, 200, {n1, n3, n2});

    FatigueDamageMap map;
    map.status = FatigueDamageMapStatus::Success;
    FatigueDamageMapEntry a;
    a.position.location = OpenModel::ResultLocation::Element;
    a.position.entityId = 100;
    a.hasDamage = true;
    a.damage = 0.2;
    map.entries.push_back(a);
    FatigueDamageMapEntry b;
    b.position.location = OpenModel::ResultLocation::Element;
    b.position.entityId = 200;
    b.hasDamage = true;
    b.damage = 0.7;
    map.entries.push_back(b);

    auto converted = FatigueDamageMapWebViewAdapter::makeElementField(map, mesh);
    assert(converted);
    assert(converted.field.name == "Fatigue damage");
    assert(converted.field.values.size() == 2);
    assert(converted.field.values[0] == 0.2);
    assert(converted.field.values[1] == 0.7);

    map.entries.pop_back();
    converted = FatigueDamageMapWebViewAdapter::makeElementField(map, mesh);
    assert(converted.status == DamageMapViewerStatus::MissingElement);

    map.entries.push_back(b);
    map.entries[0].hasDamage = false;
    converted = FatigueDamageMapWebViewAdapter::makeElementField(map, mesh);
    assert(converted.status == DamageMapViewerStatus::MissingDamage);

    map.entries[0].hasDamage = true;
    map.entries[0].position.location = OpenModel::ResultLocation::Node;
    converted = FatigueDamageMapWebViewAdapter::makeElementField(map, mesh);
    assert(converted.status == DamageMapViewerStatus::UnsupportedLocation);

    map.entries[0].position.location = OpenModel::ResultLocation::Element;
    map.entries[0].position.entityId = 200;
    converted = FatigueDamageMapWebViewAdapter::makeElementField(map, mesh);
    assert(converted.status == DamageMapViewerStatus::DuplicateElement);
    return 0;
}
