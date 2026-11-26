# Copyright (c) 2026 Nelaric Contributors
"""Run in the independent SharedCaller Editor to create its cooked lifecycle Blueprint."""
import unreal

factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.load_class(None, "/Script/SharedCaller.SharedCallerInstance"))
asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset("BP_SharedCaller", "/Game", unreal.Blueprint, factory)
if not asset or not unreal.get_editor_subsystem(unreal.EditorAssetSubsystem).save_loaded_asset(asset):
    raise RuntimeError("Could not save the SharedCaller lifecycle Blueprint")
