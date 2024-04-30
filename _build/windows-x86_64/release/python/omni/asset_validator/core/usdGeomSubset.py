from . import registerRule, BaseRuleChecker, Suggestion

from pxr import Usd, UsdGeom, UsdShade


@registerRule("Usd:Schema")
class UsdGeomSubsetChecker(BaseRuleChecker):
    """
    Backend: RuleChecker for UsdGeomSubset family name attribute requirement.
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
        return """Ensures that a valid family name attribute is set for every UsdGeomSubset that has a material binding."""

    def apply_family_name_fix(cls, stage: Usd.Stage, prim) -> None:
        subset = UsdGeom.Subset(prim)
        subset.CreateFamilyNameAttr().Set(UsdShade.Tokens.materialBind)

    def CheckPrim(self, prim):
        if not prim.IsA(UsdGeom.Subset):
            return


        hasMaterialBindingRel = False
        hasFamilyName = False

        for prop in prim.GetProperties():
            hasMaterialBindingRel = hasMaterialBindingRel or prop.GetName().startswith("material:binding")
            hasFamilyName = hasFamilyName or prop.GetName().startswith("familyName")

        subset = UsdGeom.Subset(prim)

        if (hasMaterialBindingRel and (not hasFamilyName or subset.GetFamilyNameAttr().Get() != UsdShade.Tokens.materialBind)):
            self._AddFailedCheck(
                f"GeomSubset '{prim.GetName()}' has a material binding but no valid family name attribute.",
                at=prim,
                suggestion=Suggestion(
                    self.apply_family_name_fix,
                    f"Adds the family name attribute."
                )
            )
