import functools
from typing import Set

from . import registerRule, BaseRuleChecker, Suggestion

from pxr import Sdf, Usd, UsdLux


@registerRule("Usd:Schema")
class UsdLuxSchemaChecker(BaseRuleChecker):
    """
    Backend: RuleChecker for UsdLux schema
    """

    def __init__(
        self,
        verbose: bool,
        consumerLevelChecks: bool,
        assetLevelChecks: bool
    ):
        super().__init__(verbose, consumerLevelChecks, assetLevelChecks)
        # list of attributes to check prefixes for
        self.lux_attributes: Set[str] = set(['angle', 'color', 'temperature', 'diffuse', 'specular', 'enableColorTemperature', 'exposure', 'height', 'width', 'intensity', 'length',
        'normalize', 'radius', 'shadow:color', 'shadow:distance', 'shadow:enable', 'shadow:falloff', 'shadow:falloffGamma', 'shaping:cone:angle',
        'shaping:cone:softness', 'shaping:focus', 'shaping:focusTint', 'shaping:ies:angleScale', 'shaping:ies:file',
        'shaping:ies:normalize', 'texture:format'])

    @staticmethod
    def GetDescription():
        return """In USD 21.02, Lux attributes were prefixed with inputs: to make them connectable. This rule checker
        ensure that all UsdLux attributes have the appropriate prefix."""

    @classmethod
    def fix_attribute_name(cls, stage: Usd.Stage, attribute: Usd.Attribute) -> None:
        attribute_value = attribute.Get()
        prim: Usd.Prim = attribute.GetPrim()
        old_name = attribute.GetName()
        new_name = f"inputs:{old_name}"
        new_attribute = prim.CreateAttribute(new_name, attribute.GetTypeName())
        new_attribute.Set(attribute_value)
        if attribute.ValueMightBeTimeVarying():
            time_samples = attribute.GetTimeSamples()
            for time in time_samples:
                timecode = Usd.TimeCode(time)
                new_attribute.Set(attribute.Get(timecode), timecode)

        #prim.RemoveProperty(old_name)

    def CheckPrim(self, prim):
        if not (hasattr(UsdLux, 'Light') and prim.IsA(UsdLux.Light)) and not (hasattr(UsdLux, 'LightAPI') and prim.HasAPI(UsdLux.LightAPI)):
            return

        attributes = prim.GetAuthoredAttributes()
        attribute_names = [attr.GetName() for attr in attributes]
        for attribute in attributes:
            attribute_value = attribute.Get()

            if attribute_value is None:
                continue

            if not attribute.GetName() in self.lux_attributes:
                continue

            if f"inputs:{attribute.GetName()}" in attribute_names:
                continue

            self._AddFailedCheck(
                f"UsdLux attribute {attribute.GetName()} has been renamed in USD 21.02 and should be prefixed with 'inputs:'.",
                at=attribute,
                suggestion=Suggestion(
                    self.fix_attribute_name,
                    f"Creates a new attribute {attribute.GetName()} prefixed with inputs: for compatibility."
                )
            )