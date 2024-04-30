from pxr import Usd, UsdGeom, Kind

from . import registerRule, BaseRuleChecker


# Rules in ths file are USD requirements that are not Omniverse specific,
# and we likely want to contribute it back to Pixar at some point.


@registerRule("Basic")
class KindChecker(BaseRuleChecker):
    __ROOT_KINDS = tuple(
        kind for kind in Kind.Registry.GetAllKinds()
        if Kind.Registry.IsA(kind, Kind.Tokens.assembly) or Kind.Registry.IsA(kind, Kind.Tokens.component))
    __GROUP_KINDS = tuple(kind for kind in Kind.Registry.GetAllKinds() if Kind.Registry.IsA(kind, Kind.Tokens.group))
    __VALID_KINDS = tuple(kind for kind in Kind.Registry.GetAllKinds() if kind != Kind.Tokens.model)

    @staticmethod
    def GetDescription():
        return "All kinds must be registered and conform to the rules specified in " \
               "the (USD Glossary)[https://graphics.pixar.com/usd/release/glossary.html?#usdglossary-kind]"

    @classmethod
    def is_root_model(cls, prim) -> bool:
        """Whether this is the first model in the hierarchy."""
        model: Usd.ModelAPI = Usd.ModelAPI(prim)
        if not model.IsModel():
            return False
        parent_prim: Usd.Prim = prim.GetParent()
        if parent_prim.IsPseudoRoot():
            return True
        parent_model = Usd.ModelAPI(parent_prim)
        if parent_model.IsModel():
            return False
        return True

    def CheckPrim(self, prim):
        model = Usd.ModelAPI(prim)
        kind = model.GetKind()
        if not kind:
            return
        elif self.is_root_model(prim):
            if not Kind.Registry.IsA(kind, Kind.Tokens.assembly) and not Kind.Registry.IsA(kind, Kind.Tokens.component):
                self._AddFailedCheck(
                    f'Invalid Kind "{kind}". Kind "{kind}" cannot be at the root of the Model Hierarchy. The root '
                    f'prim of a model must one of {KindChecker.__ROOT_KINDS}.',
                    at=prim,
                )
        elif kind == Kind.Tokens.model or kind not in Kind.Registry.GetAllKinds():
            self._AddFailedCheck(
                f'Invalid Kind "{kind}". Must be one of {KindChecker.__VALID_KINDS}.',
                at=prim,
            )
        elif Kind.Registry.IsA(kind, Kind.Tokens.model):
            parent = prim.GetParent()
            parent_model = Usd.ModelAPI(parent)
            parent_kind = parent_model.GetKind()
            if not Kind.Registry.IsA(parent_kind, Kind.Tokens.group):
                self._AddFailedCheck(
                    f'Invalid Kind "{kind}". Model prims can only be parented under "{KindChecker.__GROUP_KINDS}" prims,'
                    f' but parent Prim <{parent.GetName()}> has Kind "{parent_kind}".',
                    at=prim,
                )


@registerRule("Basic")
class ExtentsChecker(BaseRuleChecker):

    def __init__(self, verbose: bool, consumerLevelChecks: bool, assetLevelChecks: bool):
        super().__init__(verbose, consumerLevelChecks, assetLevelChecks)

        # Unfortunately, there's no universal way of knowing which attributes extent calculation depends on.
        # Hence we hardcode the attributes per schema in schemaTypeAttrMap
        self.__schemaTypeAttrMap = {
            UsdGeom.PointBased: ['GetPointsAttr'],
            UsdGeom.Curves: ['GetWidthsAttr'],
            UsdGeom.Points: ['GetWidthsAttr'],
        }

    @staticmethod
    def GetDescription():
        return "Boundable prims have the extent attribute. For point based prims, the value of the extent " \
               "must be correct at each time sample of the point attribute"

    def CheckPrim(self, prim):
        if not prim.IsA(UsdGeom.Boundable):
            return

        boundable = UsdGeom.Boundable(prim)

        # get the authored extent value
        authoredExtent = boundable.GetExtentAttr().Get()
        computedExtent = boundable.ComputeExtentFromPlugins(boundable, Usd.TimeCode.Default())

        # if no extent value, add failed check
        if not boundable.GetExtentAttr().HasValue():
            self._AddFailedCheck(
                "Prim does not have any extent value",
                at=prim,
            )

        # if it does have an extent value, check if computed extent is correct
        elif authoredExtent != computedExtent:
            self._AddFailedCheck(
                f"Prim has incorrect extent value. Authored Value: {authoredExtent}. Computed Value: {computedExtent}",
                at=prim,
            )

        # Check extent at each time sample
        for schema in self.__schemaTypeAttrMap:
            if not prim.IsA(schema):
                continue

            for getter in self.__schemaTypeAttrMap[schema]:
                attr = getattr(schema(prim), getter)()

                if not attr.IsValid():
                    continue

                for time in attr.GetTimeSamples():
                    authoredExtent = boundable.GetExtentAttr().Get(time)
                    computedExtent = boundable.ComputeExtentFromPlugins(boundable, time)

                    if computedExtent != authoredExtent:
                        self._AddFailedCheck(
                            f"Incorrect extent value for prim at time {time} (from {attr.GetName()}). Authored Value: "
                            f"{authoredExtent}. Computed Value: {computedExtent}",
                            at=prim,
                        )
