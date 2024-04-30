"""
Auto fix framework.

The idea behind an Auto Fix should be simple. Users should be able to `validate`, get the `Results` object,
get the `autofixes` and apply them (prior filtering).

A simple use case is as follows:

.. code-block:: python

    import omni.asset_validator.core

    engine = omni.asset_validator.core.ValidationEngine()
    results = engine.validate('omniverse://localhost/NVIDIA/Samples/Astronaut/Astronaut.usd')
    issues = results.issues()

    fixer = omni.asset_validator.core.IssueFixer('omniverse://localhost/NVIDIA/Samples/Astronaut/Astronaut.usd')
    fixer.fix(issues)
    fixer.save()

Auto Fixes may or may not be applied at all, this depends completely on the user. As such Auto Fixes may be applied
post factum (i.e. after Validation), as such we need to take the following into account:

- All USD objects accessed during validation may lose references, we need to keep the references somehow.
- We need to keep track of the actions we would have performed on the those references.

To keep references to all prims we visited we create multiple `Identifiers`. Identifiers contain the key of the
object to be accessed. Callables will tell us what action perform on those references. The idea is to create an AST
of post mortum actions.

See Also `BaseRuleChecker`
"""
from enum import Enum
from functools import lru_cache
from typing import (
    List,
    Optional,
    Callable,
    Union,
    Generic,
    TypeVar,
    Any, Set
)

import attr
from attr import define, field, asdict, frozen
from attr.validators import optional, instance_of, is_callable, deep_iterable

import omni.client
from pxr import Usd, Sdf, Tf

# Type section

T = TypeVar("T")
"""Used/Required for Generics."""

RuleType = TypeVar("RuleType")
"""Any Checker is a subclass of BaseRuleChecker."""

AtType = Union[
    Usd.Stage,
    Usd.Prim,
    Usd.Attribute,
    # TODO: And many more..
]
"""Location of an issue happens at different kind of Usd objects"""


@define
class _KeyValue:
    key = field()
    value = field()


def _dumps(obj: Any, indent: int = 0) -> str:
    """
    Internal. Converts a class into pretty string representation.
    """

    def tab(text) -> str:
        space = ' ' * (4 * indent)
        return f"{space}{text}"

    parts = []
    if isinstance(obj, _KeyValue):
        parts.append(tab(f"{obj.key.lstrip('_')}={_dumps(obj.value, indent)}"))
    elif attr.has(obj):
        parts.append(tab(f"{obj.__class__.__name__}("))
        parts.append(",\n".join(_dumps(_KeyValue(k, v), indent + 1)
                                for k, v in asdict(obj, recurse=False).items()))
        parts.append(tab(")"))
    elif type(obj) == list:
        parts.append("[")
        parts.append(",\n".join(_dumps(item, indent + 1) for item in obj))
        parts.append(tab("]"))
    elif type(obj) == str:
        parts.append(f'"{obj}"')
    elif type(obj) == type:
        parts.append(obj.__name__)
    else:
        parts.append(str(obj))
    return "\n".join(parts)


# Identifier section

class Identifier(Generic[T]):
    """
    An Identifier is a stateful representation of an Usd object (i.e. Usd.Prim). A identifier can convert back to a
    live Usd object.
    """

    @classmethod
    def from_(cls, obj: T) -> "Identifier[T]":
        """
        Args:
            obj: An Usd Object.

        Returns:
            A stateful representation of an Usd object.
        """
        raise NotImplementedError()

    def restore(self, stage: Usd.Stage) -> T:
        """
        Convert this stateful identifier to a live object.

        Returns:
            An Usd object.
        """
        raise NotImplementedError()

    def as_str(self) -> str:
        """
        Pretty print representation of this object. Useful for testing.
        """
        raise NotImplementedError()


@frozen
class StageId(Identifier[Usd.Stage]):
    """
    A unique identifier to stage, i.e. identifier.
    """
    identifier: str = field(validator=instance_of(str))

    @classmethod
    def from_(cls, stage: Usd.Stage) -> "StageId":
        return StageId(
            identifier=stage.GetRootLayer().identifier
        )

    def restore(self, stage: Usd.Stage) -> Usd.Stage:
        if stage.GetRootLayer().identifier != self.identifier:
            raise ValueError("The supplied stage do not corresponds to current stage.")
        return stage

    def as_str(self) -> str:
        return f"Stage <{self.identifier}>"


@frozen
class PrimId(Identifier[Usd.Prim]):
    """
    A unique identifier of a prim, i.e. a combination of Stage definition and prim path.
    """
    stage_ref: StageId = field(validator=instance_of(StageId))
    path: str = field(validator=instance_of(str))

    @classmethod
    def from_(cls, prim: Usd.Prim) -> "PrimId":
        return PrimId(
            stage_ref=StageId.from_(prim.GetStage()),
            path=str(prim.GetPrimPath()),
        )

    def restore(self, stage: Usd.Stage) -> Usd.Prim:
        stage = self.stage_ref.restore(stage)
        return stage.GetPrimAtPath(self.path)

    def as_str(self) -> str:
        return f"Prim <{self.path}>"


@frozen
class AttributeId(Identifier[Usd.Attribute]):
    """
    A unique identifier of an attribute, i.e. a combination of prim definition and attribute name.
    """
    prim_ref: PrimId = field(validator=instance_of(PrimId))
    name: str = field(validator=instance_of(str))

    @classmethod
    def from_(cls, attr: Usd.Attribute) -> "AttributeId":
        return AttributeId(
            prim_ref=PrimId.from_(attr.GetPrim()),
            name=attr.GetName(),
        )

    def restore(self, stage: Usd.Stage) -> Usd.Attribute:
        prim = self.prim_ref.restore(stage)
        return prim.GetAttribute(self.name)

    def as_str(self) -> str:
        return f"Attribute ({self.name}) {self.prim_ref.as_str()}"


def to_identifier(value: Optional[T]) -> Optional[Identifier[T]]:
    """
    Args:
        value: Optional. An USD object.

    Returns:
        Optional. A identifier (i.e. stateful representation) to a USD object.
    """
    if not value:
        return None
    elif isinstance(value, Usd.Attribute):
        return AttributeId.from_(value)
    elif isinstance(value, Usd.Prim):
        return PrimId.from_(value)
    elif isinstance(value, Usd.Stage):
        return StageId.from_(value)
    # TODO: And many more..
    else:
        raise NotImplementedError(f"Unknown type {value}")


# Issue section

class IssueSeverity(Enum):
    """
    Defines the severity of an issue.
    """
    ERROR = 0
    FAILURE = 1
    WARNING = 2
    NONE = 3


@frozen
class Suggestion:
    """
    A suggestion is a combination of a callable and a message describing the suggestion.
    """
    callable: Callable[[Usd.Stage, AtType], None] = field(validator=is_callable())
    message: str = field(validator=instance_of(str))

    def __call__(self, stage: Usd.Stage, at: AtType) -> None:
        self.callable(stage, at)


@frozen
class Issue:
    """Issues capture information related to Validation Rules:

    Attributes:
        message (str): The reason this issue is mentioned.
        severity (IssueSeverity): The severity associated with the issue.
        rule (BaseRuleChecker): Optional. The class of rule detecting this issue.
        at (Identifier): Optional. The Prim/Stage/Layer/SdfLayer/SdfPrim/etc.. where this issue arises.
        suggestion (Callable): Optional. The suggestion to apply in form of a ``Callable(`stage`, `at`)``. Suggestion
            evaluation (i.e. suggestion()) could raise exception, in which case they will be handled by IssueFixer
            and mark as failed.

    The following exemplifies the expected arguments of an issue:

    .. code-block:: python

        import omni.asset_validator.core

        class MyRule(BaseRuleChecker):
            pass

        stage = Usd.Stage.Open('omniverse://localhost/NVIDIA/Samples/Astronaut/Astronaut.usd')
        prim = stage.GetPrimAtPath("/");

        def my_suggestion(stage: Usd.Stage, at: Usd.Prim):
            pass

        issue = omni.asset_validator.core.Issue(
            message="The reason this issue is mentioned",
            severity=IssueSeverity.ERROR,
            rule=MyRule,
            at=stage,
            suggestion=Suggestion(my_suggestion, "A good suggestion"),
        )

    """
    message: str = field(validator=instance_of(str))
    severity: IssueSeverity = field(validator=instance_of(IssueSeverity))
    rule: Optional[RuleType] = field(default=None)
    at: Optional[Identifier[AtType]] = field(default=None, converter=to_identifier)
    suggestion: Optional[Suggestion] = field(default=None, validator=optional(instance_of(Suggestion)))

    @rule.validator
    def _check_rule(self, attribute, value):
        from .complianceChecker import BaseRuleChecker
        if value is None:
            return
        if issubclass(value, BaseRuleChecker):
            return
        raise ValueError("Rule is not of type BaseRuleChecker")

    def __str__(self) -> str:
        """
        String representation of this issue.
        """
        tokens = []
        if self.severity is IssueSeverity.NONE:
            return "No Issue found."
        if self.rule:
            if self.severity is IssueSeverity.ERROR:
                tokens.append(f"Error checking rule {self.rule.__name__}: {self.message}.")
            elif self.severity is IssueSeverity.WARNING:
                tokens.append(f"Warning checking rule {self.rule.__name__}: {self.message}.")
            elif self.severity is IssueSeverity.FAILURE:
                tokens.append(f"Failure checking rule {self.rule.__name__}: {self.message}.")
        else:
            if self.severity is IssueSeverity.ERROR:
                tokens.append(f"Error found: {self.message}.")
            elif self.severity is IssueSeverity.WARNING:
                tokens.append(f"Warning found: {self.message}.")
            elif self.severity is IssueSeverity.FAILURE:
                tokens.append(f"Failure found: {self.message}.")
        if self.at:
            tokens.append(f"At {self.at.as_str()}.")
        if self.suggestion:
            tokens.append(f"Suggestion: {self.suggestion.message}.")
        return " ".join(tokens)

    @classmethod
    def from_message(cls, severity: IssueSeverity, message: str) -> "Issue":
        """Converts legacy messages into issues

        Args:
            severity: The severity associated with the issue.
            message: The message associated with the issue.

        Returns:
            The resulting Issue.
        """
        return Issue(
            message=message,
            severity=severity,
        )

    @classmethod
    def from_(cls, severity: IssueSeverity, rule: RuleType, message: str) -> "Issue":
        """Converts legacy results into issues

        Args:
            severity: The severity associated with the issue.
            rule: The rule class producing the issue.
            message: The message associated with the issue.

        Returns:
            The resulting issue.
        """
        return Issue(
            message=message,
            severity=severity,
            rule=rule,
        )

    @classmethod
    @lru_cache(maxsize=1)
    def none(cls):
        return Issue(message="No Issue", severity=IssueSeverity.NONE)


IssuePredicate = Callable[[Issue], bool]


class IssuePredicates:
    """
    Convenient methods to filter issues. Additionally, provides `And` and `Or` predicates to chain multiple
    predicates, see example below.

    .. code-block:: python

        import omni.asset_validator.core

        issues = [
            omni.asset_validator.core.Issue.from_message(
                omni.asset_validator.core.IssueSeverity.ERROR, "This is an error"),
            omni.asset_validator.core.Issue.from_message(
                omni.asset_validator.core.IssueSeverity.WARNING, "Important warning!"),
        ]
        filtered = list(filter(
            omni.asset_validator.core.IssuePredicates.And(
                omni.asset_validator.core.IssuePredicates.IsError(),
                omni.asset_validator.core.IssuePredicates.ContainsMessage("Important"),
            ),
            issues
        ))
    """

    @staticmethod
    def IsFailure() -> IssuePredicate:
        return lambda issue: issue.severity == IssueSeverity.FAILURE

    @staticmethod
    def IsWarning() -> IssuePredicate:
        return lambda issue: issue.severity == IssueSeverity.WARNING

    @staticmethod
    def IsError() -> IssuePredicate:
        return lambda issue: issue.severity == IssueSeverity.ERROR

    @staticmethod
    def ContainsMessage(text: str) -> IssuePredicate:
        return lambda issue: text in issue.message

    @staticmethod
    def IsRule(rule: Union[str, RuleType]) -> IssuePredicate:
        return lambda issue: issue.rule.__name__ == rule if isinstance(rule, str) else issue.rule is rule

    @staticmethod
    def And(lh: IssuePredicate, rh: IssuePredicate) -> IssuePredicate:
        return lambda issue: lh(issue) and rh(issue)

    @staticmethod
    def Or(lh: IssuePredicate, rh: IssuePredicate) -> IssuePredicate:
        return lambda issue: lh(issue) or rh(issue)


# Fixing Issues.

class FixStatus(Enum):
    """
    Result of fix status.
    """
    NO_LOCATION = 0
    NO_SUGGESTION = 1
    FAILURE = 2
    SUCCESS = 3


@define
class FixResult:
    """
    FixResult is a combination of input and output to the IssueFixer.

    Attributes:
        issue: The issue originating this result. Useful for back tracing.
        status: The status of processing the issue, See `FixStatus`.
        exception: Optional. If the status is a Failure, it will contain the thrown exception.
    """
    issue: Issue = field(validator=instance_of(Issue))
    status: FixStatus = field(validator=instance_of(FixStatus))
    exception: Optional[Exception] = field(default=None, validator=optional(instance_of(Exception)))


def _ConvertToStage(stage: Union[str, Usd.Stage]):
    """
    Args:
        stage: Either str or Usd.Stage.
    Returns:
        A Usd.Stage or throws error.
    """
    if not isinstance(stage, (str, Usd.Stage)):
        raise ValueError(f"stage must be of type str or Usd.Stage, {type(stage)}.")
    if isinstance(stage, Usd.Stage):
        return stage
    else:
        return Usd.Stage.Open(Sdf.Layer.FindOrOpen(stage))


@define
class IssueFixer:
    """Fixes issues for the given Asset.

    Attributes:
        asset (Usd.Stage): An in-memory `Usd.Stage`, either provided directly or opened
            from a URI pointing to a Usd layer file.

    .. code-block:: python

        import omni.asset_validator.core

        # validate a layer file
        engine = omni.asset_validator.core.ValidationEngine()
        results = engine.validate('omniverse://localhost/NVIDIA/Samples/Astronaut/Astronaut.usd')
        issues = results.issues()

        # fix that layer file
        fixer = omni.asset_validator.core.IssueFixer('omniverse://localhost/NVIDIA/Samples/Astronaut/Astronaut.usd')
        fixer.fix(issues)
        fixer.save()

        # fix a live stage directly
        stage = Usd.Stage.Open('omniverse://localhost/NVIDIA/Samples/Astronaut/Astronaut.usd')
        engine = omni.asset_validator.core.ValidationEngine()
        results = engine.validate(stage)
        issues = results.issues()

        # fix that same stage in-memory
        fixer = omni.asset_validator.core.IssueFixer(stage)
        fixer.fix(issues)
        fixer.save()

    """
    asset: Usd.Stage = field(converter=_ConvertToStage)
    _layers: Set[Sdf.Layer] = field(init=False, factory=set)

    def _register_layers(self, notice, stage, *_) -> None:
        """
        Record the layers we modify.
        """
        self._layers.add(stage.GetEditTarget().GetLayer())

    @classmethod
    def _redirect_layer(cls, layer: Sdf.Layer):
        """
        Redirect edit target to specific layer.
        """

        def wrapper(notice, stage, *_):
            if stage.GetEditTarget().GetLayer() != layer:
                stage.SetEditTarget(layer)

        return wrapper

    def _apply_fixes(self, issues: List[Issue]) -> List[FixResult]:
        """
        Apply fixes if available.
        """
        status: List[FixResult] = []
        for issue in issues:
            if not issue.at:
                status.append(FixResult(issue, FixStatus.NO_LOCATION))
                continue
            if not issue.suggestion:
                status.append(FixResult(issue, FixStatus.NO_SUGGESTION))
                continue
            ref = issue.at
            obj = ref.restore(self.asset)
            try:
                issue.suggestion(self.asset, obj)
                status.append(FixResult(issue, FixStatus.SUCCESS))
            except Exception as error:  # noqa
                status.append(FixResult(issue, FixStatus.FAILURE, error))
        return status

    def fix(self, issues: List[Issue]) -> List[FixResult]:
        """Fix the specified issues.

        Args:
            issues: The list of issues to fix.

        Returns:
            An array with the resulting status (i.e. FixResult) of each issue.
        """
        self._layers.add(self.asset.GetEditTarget().GetLayer())
        callback = self._register_layers
        listener = Tf.Notice.Register(Usd.Notice.StageEditTargetChanged, callback, self.asset)
        try:
            return self._apply_fixes(issues)
        finally:
            listener.Revoke()

    def fix_at(self, issues: List[Issue], layer: Sdf.Layer) -> List[FixResult]:
        """Fix the specified issues persisting on a provided layer.

        Args:
            issues: The list of issues to fix.
            layer: Layer where to persist the changes.

        Returns:
            An array with the resulting status (i.e. FixResult) of each issue.
        """
        self._layers.add(layer)
        callback = self._redirect_layer(layer)
        with Usd.EditContext(self.asset, Usd.EditTarget(layer)):
            listener = Tf.Notice.Register(Usd.Notice.StageEditTargetChanged, callback, self.asset)
            try:
                return self._apply_fixes(issues)
            finally:
                listener.Revoke()

    @property
    def fixed_layers(self) -> List[Sdf.Layer]:
        """
        Returns: The layers affected by `fix` or `fix_at` methods.
        """
        return list(self._layers)

    def save(self) -> None:
        """Save the Asset to disk.
        Raises:
            IOError: If writing permissions are not granted.
        """
        # TODO: Add metadata of the changes.
        # Check the logical model.
        for layer in self.fixed_layers:
            if not layer.permissionToEdit:
                raise IOError(f"Cannot edit layer {layer}")
            if not layer.permissionToSave:
                raise IOError(f"Cannot save layer {layer}")
        # Check the physical model.
        for layer in self.fixed_layers:
            result, entry = omni.client.stat(layer.identifier)
            if result != omni.client.Result.OK:
                raise IOError(f"Layer does not exist: {layer}")
            read_only = entry.access & omni.client.AccessFlags.WRITE == 0
            if read_only:
                raise IOError(f"Read only file: {layer}")
        # Save
        self._layers.clear()
        self.asset.Save()

    def backup(self) -> None:
        # TODO: Do backups to nucleus?
        raise NotImplementedError()


@define
class Results:
    """A collection of Issues.

    Provides convenience mechanisms to filter Issues by IssuePredicates.
    """
    _asset: str = field(validator=instance_of(str))
    _issues: List[Issue] = field(validator=deep_iterable(instance_of(Issue)))

    @property
    def asset(self) -> str:
        """The Asset corresponding to these Results.
        """
        return self._asset

    @property
    def errors(self) -> List[str]:
        """
        Deprecated. Use the `issues` method.
        """
        return list(map(str, self.issues(IssuePredicates.IsError())))

    @property
    def warnings(self) -> List[str]:
        """
        Deprecated. Use the `issues` method.
        """
        return list(map(str, self.issues(IssuePredicates.IsWarning())))

    @property
    def failures(self) -> List[str]:
        """
        Deprecated. Use the `issues` method.
        """
        return list(map(str, self.issues(IssuePredicates.IsFailure())))

    def issues(self, predicate: Optional[IssuePredicate] = None) -> List[Issue]:
        """Filter Issues using an option IssuePredicate.

        Args:
            predicate: Optional. A predicate to filter the issues.

        Returns:
            A list of issues matching the predicate or all issues.
        """
        return list(filter(predicate, self._issues))

    def remove_issues(self, predicate: IssuePredicate) -> None:
        """Remove Issues from the Results.

        Convenience to purge issues that can be ignored or have already been processed.

        Args:
            predicate: Optional. A predicate to filter issues.
        """
        # TODO:
        raise NotImplementedError("Not implemented")

    def __str__(self):
        return _dumps(self, indent=0)
