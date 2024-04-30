from . import registerRule, BaseRuleChecker, Suggestion

from pxr import Usd, UsdShade


@registerRule("Usd:Schema")
class UsdMaterialBindingApi(BaseRuleChecker):
    """
    Backend: RuleChecker for Usd MaterialBindingAPI
    """

    def __init__(
        self,
        verbose: bool,
        consumerLevelChecks: bool,
        assetLevelChecks: bool
    ):
        super().__init__(verbose, consumerLevelChecks, assetLevelChecks)

    @staticmethod
    def GetDescription():
        return """Rule ensuring that the MaterialBindingAPI is applied on all prims that have a material binding property."""

    def apply_material_binding_api_fix(cls, stage: Usd.Stage, prim) -> None:
        UsdShade.MaterialBindingAPI.Apply(prim)

    def CheckPrim(self, prim):
        if prim.HasAPI(UsdShade.MaterialBindingAPI):
            return

        hasMaterialBindingRel = any(prop.GetName().startswith("material:binding") for prop in prim.GetProperties())
        if hasMaterialBindingRel and not prim.HasAPI(UsdShade.MaterialBindingAPI):
            self._AddFailedCheck(
                f"Prim '{prim.GetName()}' has a material binding but does not have the MaterialBindingApi.",
                at=prim,
                suggestion=Suggestion(
                    self.apply_material_binding_api_fix,
                    f"Applies the material binding API."
                )
            )
