import functools
from typing import Set

from . import registerRule, BaseRuleChecker, Suggestion

from pxr import Usd, UsdShade, Sdf

import omni.client


@registerRule("Omni:Material")
class OmniMaterialPathChecker(BaseRuleChecker):
    """
    Backend: RuleChecker for MDL file not properly fixed with "./"
    """

    def __init__(
        self,
        verbose: bool,
        consumerLevelChecks: bool,
        assetLevelChecks: bool
    ):
        super().__init__(verbose, consumerLevelChecks, assetLevelChecks)
        self.unresolved_paths: Set[str] = set()
        self.resolved_paths: Set[str] = set()

    @staticmethod
    def GetDescription():
        return """MDL assets require absolute paths or relative paths prefixed with "./" to resolve properly. This
        Rule suggests to prefix ambiguous MDL asset path(s) with a “./" to enforce that it is a relative path (i.e
        "./M_PlantSet_A13.mdl"). """

    @classmethod
    @functools.lru_cache(maxsize=64)
    def path_exists(cls, path) -> bool:
        status, _ = omni.client.stat(path)
        try:
            return status == omni.client.Result.OK
        except:  # noqa
            return False

    @classmethod
    def prefix(cls, relative_path) -> str:
        if relative_path.startswith("./"):
            return relative_path
        elif relative_path.startswith("/"):
            return f".{relative_path}"
        else:
            return f"./{relative_path}"

    @classmethod
    @functools.lru_cache(maxsize=64)
    def get_strongest_layer(cls, prim: Usd.Prim) -> Sdf.Layer:
        """
        Returns the strongest layer of a prim. The strongest layer is:
        - The root layer for the pseudo root.
        - If there are no composition arcs, the strongest layer of the parent.
        - If there are composition arcs, the strongest (i.e. most of the time unique) opinion.

        Args:
            prim: The prim

        Returns:
            The strongest layer for associated prim.
        """
        if prim.IsPseudoRoot():
            return prim.GetStage().GetRootLayer()

        references = Usd.PrimCompositionQuery.GetDirectReferences(prim)
        composition_arcs = references.GetCompositionArcs()
        if not composition_arcs:
            return cls.get_strongest_layer(prim.GetParent())

        for composition_arc in composition_arcs:
            layer_stack = composition_arc.GetTargetNode().layerStack
            return layer_stack.identifier.rootLayer

    def CheckUnresolvedPaths(self, unresolvedPaths):
        """
        We collect all unresolved paths, this is because all AssetResolvers are already applied and unresolved paths
        computed here are correct. Any missing path should be validated against this set.
        """
        self.unresolved_paths = set(unresolvedPaths)

    def CheckDependencies(self, usdStage, layerDeps, assetDeps):
        """
        We collect all assetDeps, this is because all AssetResolvers are already applied and resolved paths
        computed here are correct. Any existing path should be validated against this set.
        """
        self.resolved_paths = set(assetDeps)

    @classmethod
    def fix_path_callback(cls, stage: Usd.Stage, attribute: Usd.Attribute) -> None:
        """
        Suggestion to fix the path.
        """
        prim: Usd.Prim = attribute.GetPrim()
        asset_path: Sdf.AssetPath = Sdf.AssetPath(cls.prefix(attribute.Get().path))
        if not prim.GetPrimIndex().rootNode.children:
            attribute.Set(asset_path)
        else:
            strongest_layer: Sdf.Layer = cls.get_strongest_layer(prim)
            reference_node = prim.GetPrimIndex().rootNode.children[0]
            edit_target: Usd.EditTarget = Usd.EditTarget(strongest_layer, reference_node)
            with Usd.EditContext(stage, edit_target):
                attribute.Set(asset_path)

    def CheckPrim(self, prim):
        if not prim.IsA(UsdShade.Shader):
            return

        prim_absolute_path = self.get_strongest_layer(prim).identifier
        for attribute in prim.GetAttributes():
            if attribute.GetTypeName() != Sdf.ValueTypeNames.Asset:
                continue

            attribute_value = attribute.Get()
            if attribute_value is None:
                continue

            if not attribute_value.path.endswith(".mdl"):
                continue

            # If it is an absolute path
            if not attribute_value.path.startswith('.'):
                # It is an existing absolute path
                if attribute_value.path in self.resolved_paths:
                    continue

                # The absolute path does not exist, check if it could be a relative path instead.
                asset_path = omni.client.combine_urls(prim_absolute_path, self.prefix(attribute_value.path))
                if self.path_exists(asset_path):
                    self._AddFailedCheck(
                        f"Relative path {attribute_value.path} should be corrected to {self.prefix(attribute_value.path)}.",
                        at=attribute,
                        suggestion=Suggestion(
                            self.fix_path_callback,
                            f"Corrects asset path `{attribute_value.path}` to `{self.prefix(attribute_value.path)}`"
                        ),
                    )
                    continue

            # Unresolved absolute path
            if attribute_value.path in self.unresolved_paths:
                self._AddFailedCheck(
                    f"The path {attribute_value.path} does not exist.",
                    at=attribute,
                )
                continue

            # Unresolved relative path
            asset_path = omni.client.combine_urls(prim_absolute_path, attribute_value.path)
            if asset_path in self.unresolved_paths:
                self._AddFailedCheck(
                    f"The relative path {attribute_value.path} does not exist.",
                    at=attribute,
                )
                continue
