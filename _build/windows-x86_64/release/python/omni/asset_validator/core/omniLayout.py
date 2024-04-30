from . import registerRule, BaseRuleChecker, Suggestion
from pxr import Usd, UsdGeom


@registerRule("Omni:Layout")
class OmniDefaultPrimChecker(BaseRuleChecker):

    maxDiff = None

    @staticmethod
    def GetDescription():
        return "Omniverse requires a single, active, Xformable root prim, also set to the layer's defaultPrim"

    @classmethod
    def ActivatePrim(cls, _: Usd.Stage, prim: Usd.Prim) -> None:
        prim.SetActive(True)

    @classmethod
    def DeactivatePrim(cls, _: Usd.Stage, prim: Usd.Prim) -> None:
        prim.SetActive(False)

    def CheckStage(self, usdStage):
        defaultPrim = usdStage.GetDefaultPrim()
        if not defaultPrim:
            self._AddFailedCheck(
                "Stage has missing or invalid defaultPrim.",
                at=usdStage,
            )
            return

        if defaultPrim.GetParent() != usdStage.GetPrimAtPath('/'):
            self._AddFailedCheck(
                f'The default prim must be a root prim.',
                at=defaultPrim,
            )

        if not defaultPrim.IsA(UsdGeom.Xformable):
            self._AddFailedCheck(
                f'Invalid default prim of type "{defaultPrim.GetTypeName()}", which is not Xformable.',
                at=defaultPrim,
            )

        if not defaultPrim.IsActive():
            self._AddFailedCheck(
                f'The default prim must be active.',
                at=defaultPrim,
                suggestion=Suggestion(self.ActivatePrim, f"Activates Prim {defaultPrim.GetPath()}."),
            )

        if defaultPrim.IsAbstract():
            self._AddFailedCheck(
                f'The default prim cannot be abstract.',
                at=defaultPrim,
            )

        for prim in usdStage.GetPrimAtPath('/').GetChildren():
            if(
                not prim.IsActive() or
                prim.IsAbstract() or
                self.__IsPrototype(prim)
            ):
                continue

            if prim != defaultPrim:
                self._AddFailedCheck(
                    f'Invalid root prim is a sibling of the default prim <{defaultPrim.GetName()}>. '
                    f'{self.GetDescription()}',
                    at=prim,
                    suggestion=Suggestion(self.DeactivatePrim, f"Deactivates Prim {prim.GetPath()}."),
                )

    @staticmethod
    def __IsPrototype(prim):
        if hasattr(prim, 'IsPrototype'):
            return prim.IsPrototype()
        return prim.IsMaster()


@registerRule("Omni:Layout")
class OmniOrphanedPrimChecker(BaseRuleChecker):

    @staticmethod
    def GetDescription():
        return ('Prims usually need a "def" or "class" specifier, not just "over" specifiers.'
                ' However, such overs may be used to hold relationship targets, attribute connections,'
                ' or speculative opinions.')

    def CheckStage(self, usdStage):
        excluded = self._GetValidOrphanOvers(usdStage)

        for prim in usdStage.TraverseAll():
            if not prim.HasDefiningSpecifier() and not prim in excluded:
                self._AddFailedCheck(
                    'Prim has an orphaned over and does not contain the target prim/property of a relationship or '
                    'connection attribute. Ignore this message if the over was meant to specify speculative opinions.',
                    at=prim,
                )

    def _GetValidOrphanOvers(self, usdStage):
        # Get all relationships and attribute connections
        targetedPrims = set()
        for prim in usdStage.TraverseAll():
            for rel in prim.GetRelationships():
                for target in rel.GetTargets():
                    targetedPrims.add(usdStage.GetPrimAtPath(target.GetPrimPath()))

            for attribute in prim.GetAttributes():
                for connection in attribute.GetConnections():
                    targetedPrims.add(usdStage.GetPrimAtPath(connection.GetPrimPath()))

        # Check ancestors for valid orphan overs
        excluded = set()
        for prim in targetedPrims:
            while prim:
                if not prim.HasDefiningSpecifier():
                    excluded.add(prim)
                prim = prim.GetParent()
        return excluded
