#pragma once

#include <string>

namespace FlightUI::Internal {
class IdScope {
public:
  explicit IdScope(const std::string &id);
  IdScope(const IdScope &other) = delete;
  IdScope &operator=(const IdScope &other) = delete;
  ~IdScope();

private:
  bool m_Active = false;
};

class DisabledScope {
public:
  explicit DisabledScope(bool disabled);
  DisabledScope(const DisabledScope &other) = delete;
  DisabledScope &operator=(const DisabledScope &other) = delete;
  ~DisabledScope();

private:
  bool m_Active = false;
};

class ItemWidthScope {
public:
  explicit ItemWidthScope(float width);
  ItemWidthScope(const ItemWidthScope &other) = delete;
  ItemWidthScope &operator=(const ItemWidthScope &other) = delete;
  ~ItemWidthScope();

private:
  bool m_Active = false;
};

void ShowTooltipIfHovered(const std::string &tooltip);
} // namespace FlightUI::Internal
