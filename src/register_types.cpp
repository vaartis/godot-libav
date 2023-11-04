#include <gdextension_interface.h>
#include <godot_cpp/godot.hpp>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/classes/engine.hpp>

#include "godot_cpp/classes/resource_loader.hpp"
#include "godot_libav.hpp"

using namespace godot;

static Ref<ResourceFormatLoaderLibAV> resource_loader_libav;

void initialize_gdextension_types(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;

    ClassDB::register_class<VideoStreamPlaybackLibAV>();
    ClassDB::register_class<VideoStreamLibAV>();
    ClassDB::register_class<ResourceFormatLoaderLibAV>();

    resource_loader_libav.instantiate();
    ResourceLoader::get_singleton()->add_resource_format_loader(resource_loader_libav, true);
}

void uninitialize_gdextension_types(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;

    ResourceLoader::get_singleton()->remove_resource_format_loader(resource_loader_libav);
    resource_loader_libav.unref();
}

extern "C" {
    GDExtensionBool GDE_EXPORT godot_libav_library_init(
        GDExtensionInterfaceGetProcAddress p_get_proc_address,
        GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization
    ) {
        GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
        init_obj.register_initializer(initialize_gdextension_types);
        init_obj.register_terminator(uninitialize_gdextension_types);
        init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

        return init_obj.init();
    }
}
